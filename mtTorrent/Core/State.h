#pragma once
#include <string>
#include <vector>

namespace mtt
{
	struct TorrentState
	{
		TorrentState(std::vector<uint8_t>&);

		std::string downloadPath;

		struct File
		{
			bool selected;
		};
		std::vector<File> files;

		std::vector<uint8_t>& pieces;
		uint32_t lastStateTime = 0;
		bool started = false;

		void saveState(const std::string& name);
		bool loadState(const std::string& name);
	};

	struct TorrentsList
	{
		struct TorrentInfo
		{
			std::string name;
		};
		std::vector<TorrentInfo> torrents;

		void saveState();
		bool loadState();
	};
}
