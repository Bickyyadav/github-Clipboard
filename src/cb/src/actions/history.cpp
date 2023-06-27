/*  The Clipboard Project - Cut, copy, and paste anything, anytime, anywhere, all from the terminal.
    Copyright (C) 2023 Jackson Huff and other contributors on GitHub.com
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.*/
#include "../clipboard.hpp"

#if defined(_WIN32) || defined(_WIN64)
#include <fcntl.h>
#include <format>
#include <io.h>
#endif

#if defined(__linux__)
#include <linux/io_uring.h>
#endif

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <aio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace PerformAction {

void moveHistory() {
    size_t successful_entries = 0;
    std::vector<fs::path> absoluteEntryPaths;
    for (const auto& entry : copying.items) {
        try {
            unsigned long entryNum = std::stoul(entry.string());
            absoluteEntryPaths.emplace_back(path.entryPathFor(entryNum));
        } catch (fs::filesystem_error& e) {
            copying.failedItems.emplace_back(entry.string(), e.code());
            continue;
        } catch (...) {}
    }
    for (const auto& entry : absoluteEntryPaths) {
        path.makeNewEntry();
        fs::rename(entry, path.data);
        successful_entries++;
    }
    stopIndicator();
    fprintf(stderr, formatMessage("[success][inverse] ✔ [noinverse] Queued up [bold]%lu[blank][success] entries[blank]\n").data(), successful_entries);
    if (clipboard_name == constants.default_clipboard_name) updateExternalClipboards(true);
}

void history() {
    if (!copying.items.empty()) {
        moveHistory();
        return;
    }
    stopIndicator();
    auto available = thisTerminalSize();
    fprintf(stderr, "%s", formatMessage("[info]┏━━[inverse] ").data());
    Message clipboard_history_message = "[bold]Entry history for clipboard [help] %s[nobold]";
    fprintf(stderr, clipboard_history_message().data(), clipboard_name.data());
    fprintf(stderr, "%s", formatMessage(" [noinverse][info]━").data());
    auto usedSpace = (columnLength(clipboard_history_message) - 2) + clipboard_name.length() + 7;
    if (usedSpace > available.columns) available.columns = usedSpace;
    int columns = available.columns - usedSpace;
    fprintf(stderr, "%s%s", repeatString("━", columns).data(), formatMessage("┓[blank]").data());

    std::vector<std::string> dates;
    dates.reserve(path.entryIndex.size());

    size_t longestDateLength = 0;

    auto now = std::chrono::system_clock::now();

    struct stat dateInfo;
    std::string agoMessage;
    agoMessage.reserve(16);

    for (auto entry = 0; entry < path.entryIndex.size(); entry++) {
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
        stat(path.entryPathFor(entry).string().data(), &dateInfo);
        auto timeSince = now - std::chrono::system_clock::from_time_t(dateInfo.st_mtime);
        // format time like 1y 2d 3h 4m 5s
        auto years = std::chrono::duration_cast<std::chrono::years>(timeSince);
        auto days = std::chrono::duration_cast<std::chrono::days>(timeSince - years);
        auto hours = std::chrono::duration_cast<std::chrono::hours>(timeSince - days);
        auto minutes = std::chrono::duration_cast<std::chrono::minutes>(timeSince - days - hours);
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeSince - days - hours - minutes);
        if (years.count() > 0) agoMessage += std::to_string(years.count()) + "y ";
        if (days.count() > 0) agoMessage += std::to_string(days.count()) + "d ";
        if (hours.count() > 0) agoMessage += std::to_string(hours.count()) + "h ";
        if (minutes.count() > 0) agoMessage += std::to_string(minutes.count()) + "m ";
        agoMessage += std::to_string(seconds.count()) + "s";
        dates.emplace_back(agoMessage);

        if (agoMessage.length() > longestDateLength) longestDateLength = agoMessage.length();
        agoMessage.clear();
#else
        dates.push_back("n/a");
        longestDateLength = 3;
#endif
    }

/*#if defined(__linux__)                       \
    struct io_uring_params params;             \
    memset(&params, 0, sizeof(params));        \
    int ring_fd = io_uring_setup(16, &params); \
                                               \
                                               \
#el*/ #if defined (__unix__) || defined(__APPLE__) || defined(__linux__)
    int flags = fcntl(STDERR_FILENO, F_GETFL, 0);
    fcntl(STDERR_FILENO, F_SETFL, flags | O_APPEND);
    struct aiocb aio;
#endif

    auto longestEntryLength = numberLength(path.entryIndex.size() - 1);

    std::string availableColumnsAsString = std::to_string(available.columns);
    std::string batchedMessage;
    batchedMessage.reserve(50200);

    for (long entry = path.entryIndex.size() - 1; entry >= 0; entry--) {
        path.setEntry(entry);

        if (batchedMessage.size() > 50000) {
/*#if defined(__linux__)                      \
            // use io_uring for async writing \
                                              \
                                              \
#el*/ #if defined (__unix__) || defined(__APPLE__) || defined(__linux__)
            memset(&aio, 0, sizeof(struct aiocb));
            aio.aio_fildes = STDERR_FILENO;
            aio.aio_buf = static_cast<void*>(batchedMessage.data());
            aio.aio_nbytes = batchedMessage.size();
            aio_write(&aio);
#else
            fputs(batchedMessage.data(), stderr);
#endif
            batchedMessage.clear();
        }

        int widthRemaining = available.columns - (numberLength(entry) + longestEntryLength + longestDateLength + 7);

        batchedMessage += formatMessage(
                "\n[info]\033[" + availableColumnsAsString + "G┃\r┃ [bold]" + std::string(longestEntryLength - numberLength(entry), ' ') + std::to_string(entry) + "[nobold][info]│ [bold]"
                + std::string(longestDateLength - dates.at(entry).length(), ' ') + dates.at(entry) + "[nobold][info]│ "
        );

        if (path.holdsRawDataInCurrentEntry()) {
            std::string content(fileContents(path.data.raw));
            if (auto type = inferMIMEType(content); type.has_value())
                content = "\033[7m\033[1m" + std::string(type.value()) + ", " + formatBytes(content.length()) + "\033[22m\033[27m";
            else
                std::erase(content, '\n');
            batchedMessage += formatMessage("[help]" + content.substr(0, widthRemaining) + "[blank]");
            continue;
        }

        for (bool first = true; const auto& entry : fs::directory_iterator(path.data)) {
            auto filename = entry.path().filename().string();
            if (filename == constants.data_file_name && fs::is_empty(entry.path())) continue;
            int entryWidth = filename.length();

            if (widthRemaining <= 0) break;

            if (!first) {
                if (entryWidth <= widthRemaining - 2) {
                    batchedMessage += formatMessage("[help], [blank]");
                    widthRemaining -= 2;
                }
            }

            if (entryWidth <= widthRemaining) {
                std::string stylizedEntry;
                if (entry.is_directory())
                    stylizedEntry = "\033[4m" + filename + "\033[24m";
                else
                    stylizedEntry = "\033[1m" + filename + "\033[22m";
                batchedMessage += formatMessage("[help]" + stylizedEntry + "[blank]");
                widthRemaining -= entryWidth;
                first = false;
            }
        }
    }

#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)
    memset(&aio, 0, sizeof(struct aiocb));
    aio.aio_fildes = STDERR_FILENO;
    aio.aio_buf = static_cast<void*>(batchedMessage.data());
    aio.aio_nbytes = batchedMessage.size();
    aio_write(&aio);

    struct aiocb* aio_list[1] = {&aio};
    aio_suspend(aio_list, 1, nullptr);
#else
    fputs(batchedMessage.data(), stderr);
#endif

    fputs(formatMessage("[info]\n┗━━▌").data(), stderr);
    Message status_legend_message = "[help]Text, \033[1mFiles\033[22m, \033[4mDirectories\033[24m, \033[7m\033[1mData\033[22m\033[27m[info]";
    usedSpace = columnLength(status_legend_message) + 6;
    if (usedSpace > available.columns) available.columns = usedSpace;
    auto cols = available.columns - usedSpace;
    std::string bar2 = "▐" + repeatString("━", cols);
    fputs((status_legend_message() + bar2).data(), stderr);
    fputs(formatMessage("┛[blank]\n").data(), stderr);
}

void historyJSON() {
    printf("{\n");
    for (unsigned long entry = 0; entry < path.entryIndex.size(); entry++) {
        path.setEntry(entry);
        printf("    \"%lu\": {\n", entry);
        printf("        \"date\": %zu,\n", static_cast<size_t>(fs::last_write_time(path.data).time_since_epoch().count()));
        printf("        \"content\": ");
        if (path.holdsRawDataInCurrentEntry()) {
            std::string content(fileContents(path.data.raw));
            if (auto type = inferMIMEType(content); type.has_value()) {
                printf("{\n");
                printf("            \"dataType\": \"%s\",\n", type.value().data());
                printf("            \"dataSize\": %zd,\n", content.length());
                printf("            \"path\": \"%s\"\n", path.data.raw.string().data());
                printf("        }");
            } else {
                printf("\"%s\"", JSONescape(content).data());
            }
        } else if (path.holdsDataInCurrentEntry()) {
            printf("[\n");
            std::vector<fs::path> itemsInPath(fs::directory_iterator(path.data), fs::directory_iterator());
            for (const auto& entry : itemsInPath) {
                printf("            {\n");
                printf("                \"filename\": \"%s\",\n", entry.filename().string().data());
                printf("                \"path\": \"%s\",\n", entry.string().data());
                printf("                \"isDirectory\": %s\n", fs::is_directory(entry) ? "true" : "false");
                printf("            }%s\n", entry == itemsInPath.back() ? "" : ",");
            }
            printf("\n        ]");
        } else {
            printf("null");
        }
        printf("\n    }%s\n", entry == path.entryIndex.size() - 1 ? "" : ",");
    }
    printf("}\n");
}

} // namespace PerformAction
