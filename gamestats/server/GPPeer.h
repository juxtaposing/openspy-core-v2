#ifndef _GPPEER_H
#define _GPPEER_H
#include "../main.h"
#include <OS/Auth.h>
#include <OS/User.h>
#include <OS/Profile.h>
#include <OS/Mutex.h>

#include <OS/Search/Profile.h>
#include <OS/Search/User.h>

#include <OS/GPShared.h>

#include "GPBackend.h"


namespace GP {
	typedef struct {
		int profileid;
		int operation_id;
	} GPPersistRequestData;
	class Driver;

	class Peer {
	public:
		Peer(Driver *driver, struct sockaddr_in *address_info, int sd);
		~Peer();
		
		void think(bool packet_waiting);
		void handle_packet(char *data, int len);
		const struct sockaddr_in *getAddress() { return &m_address_info; }

		int GetProfileID();

		int GetSocket() { return m_sd; };

		bool ShouldDelete() { return m_delete_flag; };
		bool IsTimeout() { return m_timeout_flag; }

		int GetPing();
		void send_ping();

		void send_login_challenge(int type);
		void SendPacket(const uint8_t *buff, int len, bool attach_final = true);

	private:
		//packet handlers
		static void newGameCreateCallback(bool success, GPBackend::PersistBackendResponse response_data, GP::Peer *peer, void* extra);
		void handle_newgame(const char *data, int len);

		static void updateGameCreateCallback(bool success, GPBackend::PersistBackendResponse response_data, GP::Peer *peer, void* extra);
		void handle_updgame(const char *data, int len);

		void handle_authp(const char *data, int len);
		void handle_auth(const char *data, int len);
		void handle_getpid(const char *data, int len);

		static void getPersistDataCallback(bool success, GPBackend::PersistBackendResponse response_data, GP::Peer *peer, void* extra);
		void handle_getpd(const char *data, int len);

		static void setPersistDataCallback(bool success, GPBackend::PersistBackendResponse response_data, GP::Peer *peer, void* extra);
		void handle_setpd(const char *data, int len);

		//login
		void perform_pid_auth(int profileid, const char *response, int operation_id);
		static void m_nick_email_auth_cb(bool success, OS::User user, OS::Profile profile, OS::AuthData auth_data, void *extra, int operation_id);


		void send_error(GPShared::GPErrorCode code);
		void gamespy3dxor(char *data, int len);
		int gs_chresp_num(const char *challenge);
		void gs_sesskey(int sesskey, char *out);
		bool IsResponseValid(const char *response);


		int m_sd;
		OS::GameData m_game;


		Driver *mp_driver;

		struct sockaddr_in m_address_info;

		struct timeval m_last_recv, m_last_ping;

		bool m_delete_flag;
		bool m_timeout_flag;

		char m_challenge[CHALLENGE_LEN + 1];
		int m_session_key;


		//these are modified by other threads
		std::string m_backend_session_key;
		OS::User m_user;
		OS::Profile m_profile;

		OS::CMutex *mp_mutex;

		uint16_t m_game_port;

		std::string m_current_game_identifier;
	};
}
#endif //_GPPEER_H