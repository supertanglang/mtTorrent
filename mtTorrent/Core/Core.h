#pragma once
#include "Torrent.h"

class TcpAsyncServer;
class ServiceThreadpool;

namespace mtt
{
	namespace dht
	{
		class Communication;
	}

	class IncomingPeersListener;

	class Core
	{
	public:

		std::shared_ptr<IncomingPeersListener> listener;
		std::shared_ptr<dht::Communication> dht;

		std::vector<TorrentPtr> torrents;

		void init();
		void deinit();

		TorrentPtr addFile(const char* filename);
		TorrentPtr addMagnet(const char* magnet);

		TorrentPtr getTorrent(const uint8_t* hash);

		Status removeTorrent(const uint8_t* hash, bool deleteFiles);

	private:

		void saveTorrentFile(TorrentPtr t);
	};
}
