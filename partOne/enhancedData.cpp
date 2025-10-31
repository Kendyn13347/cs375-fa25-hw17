#include <iostream>
#include <vector>
#include <map>
#include <queue>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <random>
#include <chrono>
#include <iomanip>
#include <climits>
#include <algorithm>

enum ReplacementAlgorithm { FIFO, LRU };

enum Protection { READ_ONLY, READ_WRITE };

struct Page {
    int frame_number = -1; 
    bool present = false;
    Protection protection = READ_WRITE;
    int last_access_time = 0;
};

struct Segment {
    int base_address; 
    int limit; 
    Protection protection;
};

class PageTable; 

std::map<int, std::pair<PageTable*, int>> frame_to_page_map;


class PageTable {
public:
    std::vector<Page> pages;
    int page_size;

    PageTable(int numPages, int pSize) : page_size(pSize) {
        pages.resize(numPages);
        for (int i = 0; i < pages.size(); ++i) {
            auto& p = pages[i];
            p.frame_number = -1;
            p.present = false;
            p.protection = (rand() % 2) ? READ_WRITE : READ_ONLY;
            p.last_access_time = 0;
        }
    }

    PageTable() : page_size(1000) {} 

    int getFrameNumber(int pageNum, int time, Protection accessType) {
        if (pageNum < 0 || pageNum >= pages.size()) {
            std::cout << "Page Fault: Invalid page number " << pageNum << "\n";
            return -1;
        }

        if (accessType == READ_WRITE && pages[pageNum].protection == READ_ONLY) {
            std::cout << "Protection Violation: Cannot write to read-only page\n";
            return -1;
        }
        
        if (!pages[pageNum].present) {
            std::cout << "Page Fault: Page " << pageNum << " not in memory\n";
            return -2; 
        }

        pages[pageNum].last_access_time = time;
        if(frame_to_page_map.count(pages[pageNum].frame_number)) {
             frame_to_page_map[pages[pageNum].frame_number].first->pages[frame_to_page_map[pages[pageNum].frame_number].second].last_access_time = time;
        }

        return pages[pageNum].frame_number;
    }

    void setFrame(int pageNum, int frame, Protection prot, int time) {
        if (pageNum >= 0 && pageNum < pages.size()) {
            pages[pageNum].frame_number = frame;
            pages[pageNum].present = true;
            pages[pageNum].protection = prot;
            pages[pageNum].last_access_time = time;

            frame_to_page_map[frame] = {this, pageNum};
        }
    }

    void invalidatePage(int pageNum) {
        if (pageNum >= 0 && pageNum < pages.size()) {
            pages[pageNum].frame_number = -1;
            pages[pageNum].present = false;
        }
    }
};

class PageDirectory {
public:
    std::vector<PageTable> page_tables;
    int page_size;

    PageDirectory(int numPageDirEntries, int numPageTableEntries, int pSize) : page_size(pSize) {
        page_tables.resize(numPageDirEntries);
        for(int i = 0; i < numPageDirEntries; ++i) {
            page_tables[i] = PageTable(numPageTableEntries, pSize);
        }
    }

    PageDirectory() : page_size(1000) {}

    PageTable* getPageTable(int pageDirIndex) {
        if (pageDirIndex < 0 || pageDirIndex >= page_tables.size()) {
            return nullptr;
        }
        return &page_tables[pageDirIndex];
    }
};


class PhysicalMemory {
public:
    int num_frames;
    std::vector<bool> free_frames;
    std::queue<int> fifo_queue;
    int time = 0;
    ReplacementAlgorithm algo; 

    PhysicalMemory(int frames, ReplacementAlgorithm algorithm) 
        : num_frames(frames), algo(algorithm) {
        free_frames.resize(frames, true);
    }

    int allocateFrame() {
        for (int i = 0; i < num_frames; ++i) {
            if (free_frames[i]) {
                free_frames[i] = false;
                if (algo == FIFO) {
                    fifo_queue.push(i);
                }
                std::cout << "Allocated free frame " << i << "\n";
                return i;
            }
        }

        std::cout << "No free frames. Running page replacement...\n";
        int victimFrame = -1;

        if (algo == FIFO) {
            if (!fifo_queue.empty()) {
                victimFrame = fifo_queue.front();
                fifo_queue.pop();
                // re-enqueue victim to maintain FIFO order (frame will be reused)
                fifo_queue.push(victimFrame);
            } else {
                // fallback: choose any mapped frame if FIFO queue is empty
                if (!frame_to_page_map.empty()) {
                    victimFrame = frame_to_page_map.begin()->first;
                }
            }
            std::cout << "FIFO victim: frame " << victimFrame << "\n";

        } else if (algo == LRU) {
            int minTime = INT_MAX;
            for (auto const& entry : frame_to_page_map) {
                int frame = entry.first;
                PageTable* pt = entry.second.first;
                int pageNum = entry.second.second;
                if (pt->pages[pageNum].last_access_time < minTime) {
                    minTime = pt->pages[pageNum].last_access_time;
                    victimFrame = frame;
                }
            }
            std::cout << "LRU victim: frame " << victimFrame << "\n";
        }

        if (victimFrame != -1) {
            if (frame_to_page_map.count(victimFrame)) {
                auto& victim_page_info = frame_to_page_map[victimFrame];
                PageTable* victim_pt = victim_page_info.first;
                int victim_pageNum = victim_page_info.second;

                std::cout << "Invalidating page " << victim_pageNum
                          << " from its page table.\n";
                victim_pt->invalidatePage(victim_pageNum);

                frame_to_page_map.erase(victimFrame);
            }
            // mark victim frame as free so it can be allocated by caller
            if (victimFrame >= 0 && victimFrame < num_frames) {
                free_frames[victimFrame] = true;
            }
        }

        return victimFrame;
    }

    void freeFrame(int frame) {
        if (frame >= 0 && frame < num_frames) {
            free_frames[frame] = true;
            if(frame_to_page_map.count(frame)) {
                frame_to_page_map.erase(frame);
            }
        }
    }

    double utilization() const {
        int used = std::count(free_frames.begin(), free_frames.end(), false);
        return (double)used / num_frames * 100;
    }
};

class SegmentTable {
public:
    std::vector<Segment> segments;
    std::map<int, PageDirectory> segment_directories;
    PhysicalMemory* physMem;
    int page_size;

    SegmentTable(int numFrames, ReplacementAlgorithm algo, int pSize) 
        : page_size(pSize) {
        physMem = new PhysicalMemory(numFrames, algo);
    }
    
    ~SegmentTable() {
        delete physMem;
    }

    void addSegment(int id, int base, int limit, Protection prot, int dirSize, int tableSize) {
        segments.push_back({base, limit, prot});
        segment_directories[id] = PageDirectory(dirSize, tableSize, page_size);
    }

    int translateAddress(int segNum, int pageDir, int pageNum, int offset, Protection accessType, int& latency) {
        physMem->time++; 
        latency = 1 + rand() % 5; 

        if (segNum < 0 || segNum >= segments.size()) {
            std::cout << "Segmentation Fault: Invalid segment " << segNum << "\n";
            return -1;
        }
        Segment& segment = segments[segNum];

        if (accessType == READ_WRITE && segment.protection == READ_ONLY) {
            std::cout << "Protection Violation: Cannot write to read-only segment\n";
            return -1;
        }

        if (segment_directories.find(segNum) == segment_directories.end()) {
             std::cout << "Segmentation Fault: No page directory for segment " << segNum << "\n";
             return -1;
        }
        PageDirectory* dir = &segment_directories[segNum];

        if (pageDir < 0 || pageDir >= dir->page_tables.size()) {
             std::cout << "Page Fault: Invalid Page Directory index " << pageDir << "\n";
             return -1;
        }
        PageTable* pt = dir->getPageTable(pageDir);
        if (pt == nullptr) {
            std::cout << "Page Fault: Invalid Page Directory index " << pageDir << "\n";
            return -1;
        }
        
        if (offset < 0 || offset >= pt->page_size) {
            std::cout << "Offset Fault: Offset " << offset << " exceeds page size\n";
            return -1;
        }

        int frame = pt->getFrameNumber(pageNum, physMem->time, accessType);

        if (frame == -1) {
            return -1;
        } 
        
        if (frame == -2) { 
            latency += 100;
            
            frame = physMem->allocateFrame();
            
            if (frame == -1) {
                std::cout << "Error: Page replacement failed.\n";
                return -1;
            }

            pt->setFrame(pageNum, frame, segment.protection, physMem->time);
        }

        return (frame * pt->page_size) + offset;
    }

    void printMemoryMap() {
        std::cout << "\n--- Memory Map ---\n";
        std::cout << "Physical Memory Utilization: " << physMem->utilization() << "%\n";
        std::cout << "Frames in Use: " << frame_to_page_map.size() << "/" << physMem->num_frames << "\n";
        
        for (auto const& [frame, page_info] : frame_to_page_map) {
             PageTable* pt = page_info.first;
             int pageNum = page_info.second;
             std::cout << "  [Frame " << std::setw(2) << frame << "]:"
                       << " Page " << std::setw(2) << pageNum
                       << " (Access Time: " << pt->pages[pageNum].last_access_time << ")\n";
        }
        std::cout << "-------------------\n";
    }
};


void generateRandomAddresses(SegmentTable& st, int num, double validRatio, const std::string& logFile) {
    std::ofstream log(logFile);
    std::mt19937 gen(std::chrono::system_clock::now().time_since_epoch().count());
    int faults = 0;

    for (int i = 0; i < num; ++i) {
        int segNum, pageDir, pageNum, offset, access;
        int latency;

        if ((double)rand() / RAND_MAX < validRatio) {
            segNum = gen() % st.segments.size();
            PageDirectory& dir = st.segment_directories[segNum];
            pageDir = gen() % dir.page_tables.size();
            PageTable* pt = dir.getPageTable(pageDir);
            pageNum = gen() % pt->pages.size();
            offset = gen() % pt->page_size;
        } else {
            segNum = (gen() % st.segments.size()) + 1;
            pageDir = (gen() % 10) + 5; 
            pageNum = (gen() % 100) + 50;
            offset = (gen() % (st.page_size * 2));
        }
        
        access = (gen() % 2) ? READ_WRITE : READ_ONLY;

        std::string accessStr = (access == READ_ONLY) ? "Read" : "Write";
        log << "Address " << i << ": ("
            << "Seg: " << segNum << ", "
            << "Dir: " << pageDir << ", "
            << "Page: " << pageNum << ", "
            << "Offset: " << offset << ") "
            << "Access: " << accessStr << "\n";

        int addr = st.translateAddress(segNum, pageDir, pageNum, offset, (Protection)access, latency);
        
        if (addr == -1) {
            faults++;
            log << "  -> Failed (Error or Fault)\n";
        } else {
            log << "  -> Physical: " << addr << ", Latency: " << latency << "\n";
        }
    }
    log << "\nPage Fault/Error Rate: " << (double)faults / num * 100 << "%\n";
    std::cout << "Page Fault/Error Rate: " << (double)faults / num * 100 << "%\n";
}


int main() {
    srand(time(0));

    int algoChoice;
    std::cout << "Select Replacement Algorithm (0=FIFO, 1=LRU): ";
    std::cin >> algoChoice;
    ReplacementAlgorithm algo = (algoChoice == 1) ? LRU : FIFO;

    int numFrames, pageSize, numSegments;
    std::cout << "Enter number of physical frames: ";
    std::cin >> numFrames;
    std::cout << "Enter page size: ";
    std::cin >> pageSize;
    std::cout << "Enter number of segments: ";
    std::cin >> numSegments;

    SegmentTable segmentTable(numFrames, algo, pageSize);

    int dirSize = 4; 
    int tableSize = 16; 
    std::cout << "Using " << dirSize << " directory entries and " 
              << tableSize << " page table entries per segment.\n";

    for (int i = 0; i < numSegments; ++i) {
        int limit = dirSize;
        Protection prot = (rand() % 2) ? READ_ONLY : READ_WRITE;
        segmentTable.addSegment(i, 0, limit, prot, dirSize, tableSize);
    }

    segmentTable.printMemoryMap();

    int segNum, pageDir, pageNum, offset, access, latency;
    while (true) {
        std::cout << "\nEnter logical address (seg, pageDir, pageNum, offset, access[0=R,1=W])"
                  << " or -1 to stop: ";
        std::cin >> segNum;
        if (segNum == -1) break;
        std::cin >> pageDir >> pageNum >> offset >> access;

        Protection accessType = (access == 1) ? READ_WRITE : READ_ONLY;
        
        int physicalAddress = segmentTable.translateAddress(segNum, pageDir, pageNum, offset, accessType, latency);

        if (physicalAddress != -1) {
            std::cout << "  -> Physical Address: " << physicalAddress
                      << ", Latency: " << latency << "\n";
        }
        
        segmentTable.printMemoryMap();
    }

    std::cout << "Generate random addresses? (y/n): ";
    char genRand;
    std::cin >> genRand;
    if (genRand == 'y') {
        generateRandomAddresses(segmentTable, 200, 0.7, "results.txt");
        std::cout << "Results logged to results.txt\n";
        segmentTable.printMemoryMap();
    }

    return 0;
}