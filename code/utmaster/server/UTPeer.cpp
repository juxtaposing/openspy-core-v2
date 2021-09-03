#include <OS/OpenSpy.h>
#include <OS/Buffer.h>
#include <sstream>

#include "UTPeer.h"
#include "UTDriver.h"
#include "UTServer.h"
#include <tasks/tasks.h>

namespace UT {
	Peer::Peer(Driver *driver, INetIOSocket *sd) : INetPeer(driver, sd) {
		m_delete_flag = false;
		m_timeout_flag = false;
		gettimeofday(&m_last_ping, NULL);
		gettimeofday(&m_last_recv, NULL);
		m_state = EConnectionState_WaitChallengeResponse;
		m_config = NULL;
		
	}
	void Peer::OnConnectionReady() {
		OS::LogText(OS::ELogLevel_Info, "[%s] New connection", getAddress().ToString().c_str());
		send_challenge("111111111");
	}
	Peer::~Peer() {
		OS::LogText(OS::ELogLevel_Info, "[%s] Connection closed, timeout: %d", getAddress().ToString().c_str(), m_timeout_flag);
	}
	void Peer::think(bool packet_waiting) {
		NetIOCommResp io_resp;
		if (m_delete_flag) return;

		if (packet_waiting) {
			OS::Buffer recv_buffer;
			io_resp = this->GetDriver()->getNetIOInterface()->streamRecv(m_sd, recv_buffer);

			int len = io_resp.comm_len;

			if (len <= 0) {
				goto end;
			}
			gettimeofday(&m_last_recv, NULL);

			handle_packet(recv_buffer);

		}

	end:
		//send_ping();

		//check for timeout
		struct timeval current_time;
		gettimeofday(&current_time, NULL);
		if (current_time.tv_sec - m_last_recv.tv_sec > UTMASTER_PING_TIME * 2) {
			Delete(true);
		} else if ((io_resp.disconnect_flag || io_resp.error_flag) && packet_waiting) {
			Delete();
		}
	}
	void Peer::handle_packet(OS::Buffer recv_buffer) {
		
		ERequestType req_type;
		int len = recv_buffer.ReadInt();
		switch(m_state) {
			case EConnectionState_WaitChallengeResponse:
				handle_challenge_response(recv_buffer);
			break;
			case EConnectionState_ApprovedResponse:
				send_verified();
				m_state = EConnectionState_WaitRequest;
			break;
			case EConnectionState_WaitRequest:
				req_type = (ERequestType)recv_buffer.ReadByte();
				//printf("got req type: %d\n", req_type);
				switch(req_type) {
					case ERequestType_ServerList:
						handle_request_server_list(recv_buffer);
					break;
					case ERequestType_MOTD:
						send_motd();
					break;
					case ERequestType_NewServer:
						handle_newserver_request(recv_buffer);
						//send_server_id(1234);
					break;
				}
			break;
			case EConnectionState_Heartbeat:
			req_type = (ERequestType)recv_buffer.ReadByte() + HEARTBEAT_MODE_OFFSET;
			//printf("hb req: %08X\n", req_type - HEARTBEAT_MODE_OFFSET);
			switch(req_type) {
				case ERequestType_Heartbeat:
					handle_heartbeat(recv_buffer);
				break;
				case ERequestType_PlayerQuery:
					//printf("player query\n");
				break;
			}
			
			break;
		}
	}
	void Peer::handle_newserver_request(OS::Buffer recv_buffer) {
		int unk1 = recv_buffer.ReadInt();
		if(recv_buffer.readRemaining() > 0) {
			
			int unk2 = recv_buffer.ReadInt();
			int unk3 = recv_buffer.ReadInt();
			bool continue_parse = true;
			std::string accumulated_string;
			while(continue_parse) {			
				while(true) {
					char b = recv_buffer.ReadByte();
					if(b == 0x00 || b == 0x09) {
						if(b == 0x0) {
							continue_parse = false;
							break;
						}
						accumulated_string = "";	
					} else {
						accumulated_string += b;
					}
				}
			}
			int unk4 = recv_buffer.ReadInt();
			int unk5 = recv_buffer.ReadInt();
			continue_parse = true;
			accumulated_string = "";
			while(continue_parse) {			
				while(true) {
					char b = recv_buffer.ReadByte();
					if(b == 0x00 || b == 0x09) {
						if(b == 0x0) {
							continue_parse = false;
							break;
						}
						accumulated_string = "";	
					} else {
						accumulated_string += b;
					}
				}
			}
			OS::LogText(OS::ELogLevel_Info, "[%s] Stats Init: %s", getAddress().ToString().c_str(), accumulated_string.c_str());
		}
		send_server_id(0); //init stats backend, generate match id, for now not needed
	}
	void Peer::handle_heartbeat(OS::Buffer buffer) {
		MM::ServerRecord record;
		record.m_address = getAddress();

		std::stringstream ss;
		
		//read unknown properties
		uint8_t num_addresses = buffer.ReadByte();
		ss << "Clients (";
		while(num_addresses--) {
			std::string ip_address = Read_FString(buffer);		
			ss << ip_address << " ";			
		}
		ss << ") ";
		buffer.ReadByte(); //??
		uint32_t unk2 = buffer.ReadInt(); 

		record.m_address.port = htons(buffer.ReadShort());
		ss << " Address: " << record.m_address.ToString();

		//read more unknown properties
		buffer.ReadByte(); buffer.ReadByte(); buffer.ReadByte();
		


		int hostname_len = buffer.ReadInt();
		record.hostname = buffer.ReadNTS();
		ss << " Hostname: " << record.hostname;

		record.level = Read_FString(buffer);
		ss << " Level: " << record.level;

		record.game_group = Read_FString(buffer);
		ss << " Game group: " << record.game_group;

		int num_players = buffer.ReadInt(), max_players = buffer.ReadInt(), unk5 = buffer.ReadInt(), unk6 = buffer.ReadInt();//, unk7 = buffer.ReadInt();
		record.num_players = num_players;
		record.max_players = max_players;
		ss << " Players: (" << record.num_players << "/" << record.max_players << ") ";
		
		uint8_t unk7 = buffer.ReadByte(), unk8 = buffer.ReadByte(), unk9 = buffer.ReadByte(), num_fields = buffer.ReadByte();

		int idx = num_fields;
		while(buffer.readRemaining() > 0) {
			std::string field = Read_FString(buffer);
			std::string property = Read_FString(buffer);

			ss << "(" << field << "," << property << "), ";

			record.m_rules[field] = property;
			if(--idx <= 0) break;
		} 

		int num_player_entries = buffer.ReadShort();

		ss << " Players (";
		idx = num_player_entries;
		while(buffer.readRemaining() > 0) {
			MM::PlayerRecord player_record;
			int name_len = buffer.ReadInt();
			

			player_record.name = buffer.ReadNTS();
			ss << player_record.name << ",";
			
			record.m_players.push_back(player_record);

			if(--idx <= 0) break;
		}
		ss << ")";
		OS::LogText(OS::ELogLevel_Info, "[%s] HB: %s", getAddress().ToString().c_str(), ss.str().c_str());

		m_server_address = record.m_address;

        TaskScheduler<MM::UTMasterRequest, TaskThreadData> *scheduler = ((UT::Server *)(this->GetDriver()->getServer()))->getScheduler();
        MM::UTMasterRequest req;        
        req.type = MM::UTMasterRequestType_Heartbeat;
		req.peer = this;
		req.peer->IncRef();
		req.callback = NULL;
		req.record = record;
        scheduler->AddRequest(req.type, req);

	}
	void Peer::send_server_id(int id) {
		m_state = EConnectionState_Heartbeat;
		OS::Buffer send_buffer;
		send_buffer.WriteByte(3);
		send_buffer.WriteInt(id);
		send_packet(send_buffer);
	}

	void Peer::handle_challenge_response(OS::Buffer buffer) {
		std::string cdkey_hash = Read_FString(buffer);
		std::string cdkey_response = Read_FString(buffer);
		std::string client = Read_FString(buffer);
		uint32_t running_version = (buffer.ReadInt());
		uint8_t running_os = (buffer.ReadByte()) - 3;
		std::string language = Read_FString(buffer);
		uint32_t gpu_device_id = (buffer.ReadInt());
		uint32_t vendor_id = (buffer.ReadInt());
		uint32_t cpu_cycles = (buffer.ReadInt());
		uint32_t running_gpu = (buffer.ReadInt());
		OS::LogText(OS::ELogLevel_Info, "[%s] Got Game Info: cdkey hash: %s, cd key response: %s, client: %s, version: %d, os: %d, language: %s, gpu device id: %04x, gpu vendor id: %04x, cpu cycles: %d, running cpu: %d", getAddress().ToString().c_str(), cdkey_hash.c_str(), cdkey_response.c_str(), client.c_str(), running_version, running_os, language.c_str(),
			gpu_device_id, vendor_id, cpu_cycles, running_gpu
		);

		m_config = ((UT::Driver *)this->GetDriver())->FindConfigByClientName(client);
		if(m_config == NULL) {
			send_challenge_response("MODIFIED_CLIENT");
			Delete();
			return;
		}
		send_challenge_authorization();
	}

	void Peer::send_challenge(std::string challenge_string) {
		OS::Buffer send_buffer;
		Write_FString(challenge_string, send_buffer);
		send_packet(send_buffer);
	}
	void Peer::send_challenge_response(std::string response) {
		OS::Buffer send_buffer;
		Write_FString(response, send_buffer);
		send_buffer.WriteInt(3);		
		send_packet(send_buffer);
	}
	void Peer::send_challenge_authorization() {
		if(m_config->is_server) {
			m_state = EConnectionState_WaitRequest;
		} else {
			m_state = EConnectionState_ApprovedResponse;
		}
		send_challenge_response("APPROVED");
		
	}
	void Peer::send_verified() {
		OS::Buffer send_buffer;
		Write_FString("VERIFIED", send_buffer);
		send_packet(send_buffer);
	}
	void Peer::on_get_server_list(MM::MMTaskResponse response) {
		OS::Buffer send_buffer;
		send_buffer.WriteInt(0x05);
		send_buffer.WriteInt(response.server_records.size());
		send_buffer.WriteByte(1);
		
		std::vector<MM::ServerRecord>::iterator it = response.server_records.begin();
		while(it != response.server_records.end()) {
			MM::ServerRecord server = *it;

			OS::Buffer server_buffer;
			
			server_buffer.WriteInt(server.m_address.ip);
			server_buffer.WriteShort(server.m_address.port); //game port
			server_buffer.WriteShort(server.m_address.port + 1); //query port (maybe this can be something other than +1?)

			Write_FString(server.hostname, server_buffer);
			Write_FString(server.level, server_buffer);
			Write_FString(server.game_group, server_buffer);

			server_buffer.WriteByte(server.num_players);
			server_buffer.WriteByte(server.max_players);
			server_buffer.WriteInt(-1); //flags
			server_buffer.WriteShort(12546); //?
			server_buffer.WriteByte(0);

			send_buffer.WriteInt(server_buffer.bytesWritten());
			send_buffer.WriteBuffer(server_buffer.GetHead(),server_buffer.bytesWritten());
		
			it++;
		}

		NetIOCommResp io_resp = response.peer->GetDriver()->getNetIOInterface()->streamSend(response.peer->m_sd, send_buffer);
		if(io_resp.disconnect_flag || io_resp.error_flag) {
			OS::LogText(OS::ELogLevel_Info, "[%s] Send Exit: %d %d", response.peer->getAddress().ToString().c_str(), io_resp.disconnect_flag, io_resp.error_flag);
		}

	}
	void Peer::handle_request_server_list(OS::Buffer recv_buffer) {
		if(m_config->is_server) return; //???

		std::stringstream ss;

		char num_filter_fileds = recv_buffer.ReadByte();
		//printf("num_filter_fileds: %d\n", num_filter_fileds);
		for(int i=0;i<num_filter_fileds;i++) {
			int field_len = recv_buffer.ReadByte(); //skip string len
			if(field_len == 0 || field_len == 4) { ///xxxx?? why 4?? game bug?
				i--; //don't include blank field as part of query
				continue;
			}
			std::string field = recv_buffer.ReadNTS();
			int property_len = recv_buffer.ReadByte();
			if(property_len == 0) {
				i--;  //don't include blank field as part of query
				continue;
			}
			std::string property = recv_buffer.ReadNTS();
			ss << field << "="  << property << ",";
			//printf("%s (%d) = %s (%d)\n", field.c_str(), field_len, property.c_str(), property_len);
		}

		OS::LogText(OS::ELogLevel_Info, "[%s] Server list request: %s", getAddress().ToString().c_str(), ss.str().c_str());

        TaskScheduler<MM::UTMasterRequest, TaskThreadData> *scheduler = ((UT::Server *)(this->GetDriver()->getServer()))->getScheduler();
        MM::UTMasterRequest req;        
        req.type = MM::UTMasterRequestType_ListServers;
		req.peer = this;
		req.peer->IncRef();
		req.callback = on_get_server_list;
        scheduler->AddRequest(req.type, req);

	}
	void Peer::send_motd() {
		OS::Buffer send_buffer;

		Write_FString(m_config->motd, send_buffer);
		
		send_buffer.WriteInt(0); //??
		send_packet(send_buffer);
	}

	void Peer::send_packet(OS::Buffer buffer) {
		OS::Buffer send_buffer;
		send_buffer.WriteInt((uint32_t)buffer.bytesWritten());
		

		send_buffer.WriteBuffer((char *)buffer.GetHead(), buffer.bytesWritten());
		NetIOCommResp io_resp = this->GetDriver()->getNetIOInterface()->streamSend(m_sd, send_buffer);
		if(io_resp.disconnect_flag || io_resp.error_flag) {
			OS::LogText(OS::ELogLevel_Info, "[%s] Send Exit: %d %d", getAddress().ToString().c_str(), io_resp.disconnect_flag, io_resp.error_flag);
			Delete();
		}
	}

	void Peer::Delete(bool timeout) {
		delete_server();

		m_timeout_flag = timeout;
		m_delete_flag = true;
		
	}

	std::string Peer::Read_FString(OS::Buffer &buffer) {
		uint8_t length = buffer.ReadByte();
		std::string result;
		if(length > 0) {
			result = buffer.ReadNTS();
		}
		return result;
	}

	void Peer::Write_FString(std::string input, OS::Buffer &buffer) {
		buffer.WriteByte(input.length() + 1);
		buffer.WriteNTS(input);
	}
	int Peer::GetGameId() {
		if(m_config == NULL) return 0;
		return m_config->gameid;
	}
	void Peer::delete_server() {
		if(m_config != NULL && m_config->is_server) {
			TaskScheduler<MM::UTMasterRequest, TaskThreadData> *scheduler = ((UT::Server *)(this->GetDriver()->getServer()))->getScheduler();
			MM::UTMasterRequest req;        
			req.type = MM::UTMasterRequestType_DeleteServer;
			req.peer = this;
			req.peer->IncRef();
			req.record.m_address = m_server_address;
			req.callback = NULL;
			scheduler->AddRequest(req.type, req);
		}
	}
}