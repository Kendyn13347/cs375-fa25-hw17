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
#include <sstream> 
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

// Forward declare PageTable type for global map (pointer use is fine)
class PageTable; 

// global mapping of physical frame -> (page table ptr, page number)
std::map<int, std::pair<PageTable*, int>> frame_to_page_map;


// Full definition of PageTable (must come before PageDirectory which stores PageTable by value)
class PageTable {
public:
    std::vector<Page> pages;
    int page_size;

    PageTable(int numPages, int pSize) : page_size(pSize) {
        pages.resize(numPages);
        for (auto& p : pages) {
            p.present = false; 
            p.protection = (rand() % 2) ? READ_WRITE : READ_ONLY;
            p.frame_number = -1;
            p.last_access_time = 0;
        }
    }
    
    PageTable() : page_size(1000) { 
        pages.resize(100); 
        for (auto& p : pages) {
            p.present = false;
            p.protection = READ_WRITE;
            p.frame_number = -1;
            p.last_access_time = 0;
        }
    }

    int getFrameNumber(int pageNum, int time, Protection accessType) {
        if (pageNum < 0 || pageNum >= (int)pages.size()) {
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
        return pages[pageNum].frame_number;
    }

    void setFrame(int pageNum, int frame, Protection prot, int time) {
        if (pageNum >= 0 && pageNum < (int)pages.size()) {
            pages[pageNum].frame_number = frame;
            pages[pageNum].present = true;
            pages[pageNum].protection = prot;
            pages[pageNum].last_access_time = time;

            frame_to_page_map[frame] = {this, pageNum};
        }
    }

    void invalidatePage(int pageNum) {
        if (pageNum >= 0 && pageNum < (int)pages.size()) {
            pages[pageNum].frame_number = -1;
            pages[pageNum].present = false;
        }
    }
};


class PageDirectory {
public:
    std::map<int, PageTable> page_tables; 
    int page_table_size; 

    PageDirectory(int defaultPageTableSize = 100) : page_table_size(defaultPageTableSize) {}

    PageTable* getPageTable(int pageDirIndex) {
        if (page_tables.find(pageDirIndex) == page_tables.end()) {
            std::cout << "Page Fault: Invalid page directory index " << pageDirIndex << "\n";
            return nullptr;
        }
        return &page_tables[pageDirIndex];
    }
    
    void addPageTable(int pageDirIndex, int numPages, int pageSize) {
         page_tables[pageDirIndex] = PageTable(numPages, pageSize);
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
                std::cout << "-> Allocated free frame " << i << "\n";
                return i;
            }
        }

        std::cout << "-> No free frames. Running page replacement...\n";
        int victimFrame = -1;

        if (algo == FIFO) {
            if (fifo_queue.empty()) return -1; 
            victimFrame = fifo_queue.front();
            fifo_queue.pop();
            fifo_queue.push(victimFrame);
            std::cout << "-> FIFO victim: frame " << victimFrame << "\n";

        } else { 
            int minTime = INT_MAX;
            
            for(auto const& [frame, page_info] : frame_to_page_map) {
                PageTable* pt = page_info.first;
                int pageNum = page_info.second;
                if (pt->pages[pageNum].last_access_time < minTime) {
                    minTime = pt->pages[pageNum].last_access_time;
                    victimFrame = frame;
                }
            }
            std::cout << "-> LRU victim: frame " << victimFrame << "\n";
        }

        if (victimFrame != -1) {
            if (frame_to_page_map.count(victimFrame)) {
                auto& victim_page_info = frame_to_page_map[victimFrame];
                PageTable* victim_pt = victim_page_info.first;
                int victim_pageNum = victim_page_info.second;
                
                std::cout << "-> Evicting page " << victim_pageNum 
                          << " from frame " << victimFrame << ".\n";
                
                victim_pt->invalidatePage(victim_pageNum);
                
                frame_to_page_map.erase(victimFrame); 
            }
            // leave victimFrame marked free (caller will allocate it)
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

    SegmentTable(int numFrames, int pSize, ReplacementAlgorithm algo) 
        : page_size(pSize) {
        physMem = new PhysicalMemory(numFrames, algo);
    }
    
    ~SegmentTable() {
        delete physMem; 
    }

    void addSegment(int id, int base, int limit, Protection prot, int dirSize, int tableSize) {
        segments.push_back({base, limit, prot});
        segment_directories[id] = PageDirectory(tableSize);
        for(int i=0; i < dirSize; ++i) {
             segment_directories[id].addPageTable(i, tableSize, page_size);
        }
    }

    int translateAddress(int segNum, int pageDir, int pageNum, int offset, Protection accessType, int& latency) {
        physMem->time++;
        latency = 1 + rand() % 5; 

        if (segNum < 0 || segNum >= (int)segments.size()) {
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

        PageTable* pt = dir->getPageTable(pageDir);
        if (pt == nullptr) {
            return -1;
        }
        
        if (pageNum < 0 || pageNum >= (int)pt->pages.size()) {
             std::cout << "Page Fault: Page " << pageNum << " exceeds page table limit\n";
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
            std::cout << "-> Handling Page Fault...\n";
            latency += 100;
            
            frame = physMem->allocateFrame(); 
            if (frame == -1) {
                std::cout << "Error: No frames available and replacement failed.\n";
                return -1;
            }
            
            pt->setFrame(pageNum, frame, segment.protection, physMem->time);
        }

        return (frame * pt->page_size) + offset;
    }

    void printMemoryMap() {
        std::cout << "\n--- Memory Map ---\n";
        std::cout << "Physical Memory Utilization: " << physMem->utilization() << "%\n";
        std::cout << "Current Time: " << physMem->time << "\n";
        
        std::cout << "Frames in Use: \n";
        for (auto const& [frame, page_info] : frame_to_page_map) {
             PageTable* pt = page_info.first;
             int pageNum = page_info.second;
             std::cout << "  [Frame " << std::setw(2) << frame << "]:"
                       << " Page " << std::setw(2) << pageNum
                       << " (Last Access: " << pt->pages[pageNum].last_access_time << ")\n";
        }
        std::cout << "-------------------\n";
    }
};

void loadConfigFromFile(SegmentTable& st, const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cout << "Error: Could not open config file " << filename << ". Using random init.\n";
        return;
    }
    
    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        lineNum++;
        if (line.empty() || line[0] == '#') {
            continue; 
        }
        
        std::stringstream ss(line);
        int segId, dirSize, tableSize, protInt;
        
        if (ss >> segId >> dirSize >> tableSize >> protInt) {
            Protection prot = (protInt == 1) ? READ_WRITE : READ_ONLY;
            st.addSegment(segId, 0, dirSize, prot, dirSize, tableSize);
            std::cout << "Loaded segment " << segId << " from file.\n";
        } else {
            std::cout << "Warning: Skipping malformed line " << lineNum << " in config file.\n";
        }
    }
    file.close();
}


void generateRandomAddresses(SegmentTable& st, int num, double validRatio, const std::string& logFile) {
    std::ofstream log(logFile);
    std::mt19937 gen(std::chrono::system_clock::now().time_since_epoch().count());
    int faults = 0;
    long total_latency = 0;
    int successful_translations = 0;

    if (st.segments.empty()) return;

    for (int i = 0; i < num; ++i) {
        int segNum, pageDir, pageNum, offset, access;
        int latency;
        
        segNum = gen() % st.segments.size();
        PageDirectory& dir = st.segment_directories[segNum];
        if (dir.page_tables.empty()) continue;
        // pick random page directory index from keys
        auto it = dir.page_tables.begin();
        std::advance(it, gen() % dir.page_tables.size());
        pageDir = it->first;
        PageTable* pt = dir.getPageTable(pageDir);
        if(!pt) continue; 
        
        pageNum = gen() % pt->pages.size();
        offset = gen() % pt->page_size;
        access = (gen() % 2) ? READ_WRITE : READ_ONLY;

        std::string accessStr = (access == READ_ONLY) ? "Read" : "Write";
        
        int addr = st.translateAddress(segNum, pageDir, pageNum, offset, (Protection)access, latency);
        
        std::string logicAddr = "(" + std::to_string(segNum) + "," + std::to_string(pageDir) 
                              + "," + std::to_string(pageNum) + "," + std::to_string(offset) + ")";

        if (addr == -1) {
            faults++;
            log << "Time " << st.physMem->time << ": " << "Logical " << logicAddr 
                << " -> FAULT" << " (Latency: " << latency << ")\n";
        } else {
            successful_translations++;
            total_latency += latency;
            log << "Time " << st.physMem->time << ": " << "Logical " << logicAddr 
                << " -> Physical " << addr << " (Latency: " << latency << ")\n";
        }
    }
    
    log << "\n--- Stress Test Metrics ---\n";
    std::cout << "\n--- Stress Test Metrics ---\n";
    
    double faultRate = (double)faults / num * 100;
    log << "Page Fault/Error Rate: " << faultRate << "%\n";
    std::cout << "Page Fault/Error Rate: " << faultRate << "%\n";

    double avgLatency = (successful_translations > 0) ? (double)total_latency / successful_translations : 0;
    log << "Average Translation Latency: " << avgLatency << "\n";
    std::cout << "Average Translation Latency: " << avgLatency << "\n";
    
    log << "Final Memory Utilization: " << st.physMem->utilization() << "%\n";
    std::cout << "Final Memory Utilization: " << st.physMem->utilization() << "%\n";
}


int main() {
    srand(time(0));

    int algoChoice;
    std::cout << "Select Replacement Algorithm (0=FIFO, 1=LRU): ";
    std::cin >> algoChoice;
    ReplacementAlgorithm algo = (algoChoice == 1) ? LRU : FIFO;

    int numFrames, pageSize;
    std::cout << "Enter number of physical frames: ";
    std::cin >> numFrames;
    std::cout << "Enter page size: ";
    std::cin >> pageSize;

    SegmentTable segmentTable(numFrames, pageSize, algo);

    char loadFile;
    std::cout << "Load configuration from config.txt? (y/n): ";
    std::cin >> loadFile;

    if (loadFile == 'y' || loadFile == 'Y') {
        loadConfigFromFile(segmentTable, "config.txt");
    } else {
        int numSegments;
        std::cout << "Enter number of segments to randomly initialize: ";
        std::cin >> numSegments;
        
        int dirSize = 4; 
        int tableSize = 16;
        std::cout << "Initializing segments with " << dirSize << " directory entries and " 
                  << tableSize << " page table entries.\n";

        for (int i = 0; i < numSegments; ++i) {
            int limit = dirSize;
            Protection prot = (rand() % 2) ? READ_ONLY : READ_WRITE;
            segmentTable.addSegment(i, 0, limit, prot, dirSize, tableSize);
        }
    }

    if (segmentTable.segments.empty()) {
        std::cout << "No segments loaded or initialized. Exiting.\n";
        return 1;
    }

    segmentTable.printMemoryMap();

    int segNum, pageDir, pageNum, offset, access;
    long total_latency = 0;
    int total_translations = 0;

    while (true) {
        std::cout << "\nEnter logical address (seg, pageDir, pageNum, offset, access[0=R,1=W])"
                  << " or -1 to stop: ";
        std::cin >> segNum;
        if (segNum == -1) break;
        std::cin >> pageDir >> pageNum >> offset >> access;
        
        int latency;
        Protection accessType = (access == 1) ? READ_WRITE : READ_ONLY;
        
        int physicalAddress = segmentTable.translateAddress(segNum, pageDir, pageNum, offset, accessType, latency);

        total_translations++;
        total_latency += latency;
        std::string logicAddr = "(" + std::to_string(segNum) + "," + std::to_string(pageDir) 
                              + "," + std::to_string(pageNum) + "," + std::to_string(offset) + ")";

        if (physicalAddress != -1) {
            std::cout << "Time " << segmentTable.physMem->time << ": "
                      << "Logical " << logicAddr
                      << " -> Physical " << physicalAddress << " (Latency: " << latency << ")\n";
        } else {
             std::cout << "Time " << segmentTable.physMem->time << ": "
                      << "Logical " << logicAddr
                      << " -> FAULT" << " (Latency: " << latency << ")\n";
        }
        
        segmentTable.printMemoryMap();
    }

    if (total_translations > 0) {
        std::cout << "\n--- Manual Session Metrics ---\n";
        std::cout << "Average Translation Latency: " << (double)total_latency / total_translations << "\n";
    }

    std::cout << "Generate random addresses? (y/n): ";
    char genRand;
    std::cin >> genRand;
    if (genRand == 'y' || genRand == 'Y') {
        generateRandomAddresses(segmentTable, 200, 0.7, "results.txt");
        std::cout << "Stress test results logged to results.txt\n";
    }

    return 0;
}