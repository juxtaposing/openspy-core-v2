#ifndef _PEERCHATDRIVER_H
#define _PEERCHATDRIVER_H
#include <stdint.h>
#include "../main.h"
#include <OS/Net/drivers/TCPDriver.h>

#include "Peer.h"

#include <map>
#include <vector>
#ifdef _WIN32
#include <time.h>
#else
#include <sys/time.h>
#endif

namespace Peerchat {
	class Peer;
	class Driver;

	class FindPeerByUserSummaryState {
		public:
			UserSummary summary;
			Peer *peer;
	} ;

	class SendMessageIteratorState {
		public:
			std::string fromSummary;
			std::string messageType;
			std::string message;
			bool includeSelf;
			int requiredChanUserModes;
			int requiredOperFlags;

			std::string type;
			ChannelUserSummary from;
			ChannelSummary channel;
			ChannelUserSummary target;
	};

	class SetChannelKeysIteratorState {
		public:
			ChannelSummary summary;
			UserSummary user_summary;
			OS::KVReader keys;
			std::string keys_message;
	};

	class OnChannelBroadcastState {
		public:
			std::string type;
			UserSummary target;
			std::map<int, int> channel_list;
			std::string message;
			bool includeSelf;
	};

	class Driver : public TCPDriver {
	public:
		Driver(INetServer *server, const char *host, uint16_t port, bool proxyHeaders = false);
		Peer *FindPeerByProfileID(int profileid);		
		Peer *FindPeerByUserSummary(UserSummary summary);
		void SendUserMessageToVisibleUsers(std::string fromSummary, std::string messageType, std::string message, bool includeSelf = true);
		void OnChannelMessage(std::string type, ChannelUserSummary from, ChannelSummary channel, std::string message, ChannelUserSummary target, bool includeSelf, int requiredChanUserModes, int requiredOperFlags);
		void OnSetUserChannelKeys(ChannelSummary summary, UserSummary user_summary, OS::KVReader keys);
		void OnSetChannelKeys(ChannelSummary summary, OS::KVReader keys);
		void OnChannelBroadcast(std::string type, UserSummary target, std::map<int, int> channel_list, std::string message, bool includeSelf);
	protected:
		virtual INetPeer *CreatePeer(INetIOSocket *socket);

	private:
		static bool LLIterator_FindPeerByUserSummary(INetPeer* peer, FindPeerByUserSummaryState* state);
		static bool LLIterator_SendUserMessageToVisibleUsers(INetPeer* peer, SendMessageIteratorState* state);
		static bool LLIterator_OnChannelMessage(INetPeer* peer, SendMessageIteratorState* state);
		static bool LLIterator_OnSetUserChannelKeys(INetPeer* peer, SetChannelKeysIteratorState* state);
		static bool LLIterator_OnSetChannelKeys(INetPeer* peer, SetChannelKeysIteratorState* state);
		static bool LLIterator_OnChannelBroadcast(INetPeer* peer, OnChannelBroadcastState* state);
		//
	};
}
#endif //_SBDRIVER_H