#include "schedule_algo.h"
#include <map>
#include <fstream>
#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;


void read_file(string in_file_path, vector<vector<int>> &graph){
    ifstream in_file(in_file_path);
    int shape_h, shape_w;
    in_file >> shape_h >> shape_w;
    graph.resize(shape_h);
    for(int i = 0; i < shape_h; i++){
        graph[i].resize(shape_w);
        for(int j = 0; j < shape_w; j++){
            in_file >> graph[i][j];
        }
    }
}

void gen_job_table(vector<vector<int>> &graph, vector<vector<int>> &job_list){
    int unit_num = graph.size();
    job_list.resize(unit_num);
    for(int graph_id = 0; graph_id < unit_num; graph_id++){
        for(int unit_id = 0; unit_id < unit_num; unit_id++){
            if(graph[graph_id][unit_id]){
                job_list[unit_id].push_back(graph_id);
            }
        }
    }
}

bool run(unit_mesh_t &mesh, schedule_table table, bool verbose = true){
    mesh.init();
    if (verbose)
        mesh.print_status();
    for(auto item: table){
        mesh.communicate(item);
        mesh.update();
        if(!mesh.check()){
            printf("Error\n");
            return false;
        }
        if (verbose)
            mesh.print_status();
    }
    if (verbose)
        for(int i = 0; i < mesh.size * mesh.size; i++)
            mesh.mesh[i].print_log();
    return true;
}


typedef bool (*AlgPtr)(vector<vector<int>> &job_list, schedule_table &table, bool verbose);

int main(int argc, char const *argv[]) {

    // reflection
    std::map<std::string, AlgPtr> Algorithms;
    Algorithms["baseline"] = gen_baseline;
    Algorithms["greedy"] = gen_naive_greedy;
    Algorithms["weighted_matching"] = gen_weighted_matching;

    if (argc > 2) {
        // run with any parameter will set default data and algorithm
        string filename = "./input/standard_jobs/roadNet.txt";
        string alg = "baseline";
        // parsing parameters 
        for(int i = 1; i < argc; i += 2) {
            if (argv[i][0] == '-' && strlen(argv[i]) > 1 && argv[i][1] == 'f') {
                filename = argv[i+1];
            }
            if (argv[i][0] == '-' && strlen(argv[i]) > 1 && argv[i][1] == 'a') {
                if (Algorithms.find(argv[i+1])!= Algorithms.end()) {
                    alg = argv[i+1];
                } else {
                    printf("Unknown algorithm:%s\n", argv[i+1]);
                    return -1;  
                }
            }
        }
        unit_mesh_t mesh;
        vector<vector<int>> job_list;
        vector<vector<int>> graph;
        read_file(filename, graph);
        gen_job_table(graph, job_list);
        schedule_table table;
        if (!Algorithms[alg](job_list, table, true)) {
            std::cout << "Algorithm " << alg << " error" << std::endl;
            return -1;
        }
        config_mesh(job_list, mesh);
        run(mesh, table);
        std::cout << "using file: " << filename << std::endl;
        std::cout << "using algorithm: " << alg << std::endl;
    } else {
        // run benchmark
        std::string data_path = "./input";
        if (argc == 2) {
            data_path = argv[1];
        }
        std::map<std::string, std::vector<double> > normalized_cycles;
        std::map<std::string, std::vector<double> > utilizations;
        std::map<std::string, int > wins;
        for (auto alg: Algorithms) {
            wins[alg.first] = 0;
        }

        int file_cnt = 0;
        // for (auto const& data_file : std::filesystem::directory_iterator(data_path)) {
        for (auto const& data_file : std::filesystem::recursive_directory_iterator(data_path)) {
            if (!fs::is_directory(data_file)) {
                file_cnt++;
                std::cout << data_file << '\n';
                vector<vector<int>> graph;
                vector<vector<int>> job_list;
                read_file(data_file.path(), graph);
                gen_job_table(graph, job_list);

                int best_cycles = ~0u>>1;
                std::map<std::string, int > cycles;
                for (auto alg: Algorithms) {
                    std::cout << alg.first << std::endl;
                    schedule_table table;
                    unit_mesh_t mesh;
                    if (!alg.second(job_list, table, false)) {
                        std::cout << "Algorithm " << alg.first << " on data " << data_file.path() << " error" << std::endl;
                        continue;
                    }
                    config_mesh(job_list, mesh);
                    if (run(mesh, table, false)) {
                        double util = 0;
                        for(int i = 0; i < mesh.size * mesh.size; i++)
                            util += mesh.mesh[i].jobs.size() / double(mesh.mesh[i].visited.size());
                        util /= mesh.size * mesh.size;
                        normalized_cycles[alg.first].push_back(double(mesh.cycles) / (mesh.size * mesh.size - 1));
                        utilizations[alg.first].push_back(util);
                        cycles[alg.first] = mesh.cycles;
                        best_cycles = std::min(best_cycles, mesh.cycles);
                        std::cout << mesh.cycles << " " << util << std::endl;
                    }
                }

                for (auto alg: Algorithms) {
                    if (cycles[alg.first] == best_cycles) {
                        wins[alg.first]++;
                    }
                }

                std::cout << std::endl;
            }
        }
        if (file_cnt == 0) {
            std::cout << "Do not found input files\n";
        } else {
            std::cout << "\nConclude: \n";
            std::cout << "\nTot files: " << file_cnt << std::endl;

            std::vector<double> mixed_speedup(file_cnt, 1);
            std::vector<double> mixed_utilizations(file_cnt, 1);

            for (auto alg: Algorithms) {
                std::cout << "\n" << alg.first << ": \n";
                if (normalized_cycles[alg.first].size() != file_cnt) {
                    std::cout << "Algorithm error\n";
                } else {
                    double avarage_speedup = 0;
                    double avarage_utilizations = 0;
                    for (int i = 0; i < file_cnt; i++) {
                        double speedup = normalized_cycles[alg.first][i];
                        avarage_speedup += speedup;
                        double util = utilizations[alg.first][i];
                        avarage_utilizations += util;

                        int strategy_flag = 0;
                        if (mixed_speedup[i] > speedup) {
                            mixed_speedup[i] = speedup;
                            strategy_flag = 1;
                        } else if (mixed_speedup[i] == speedup) {
                            strategy_flag = 2;
                        }
                        if (strategy_flag == 1) {
                            mixed_utilizations[i] = util;
                        } else if (strategy_flag == 2) {
                            // Here min instead of max, because when speedup is the same, the lower the utilization rate, the lower the energy consumption. 
                            mixed_utilizations[i] = std::min(mixed_utilizations[i], util);
                        }
                    }
                    avarage_speedup = avarage_speedup / file_cnt;
                    avarage_utilizations = avarage_utilizations / file_cnt;

                    std::cout << "avarage speedup: " << avarage_speedup << std::endl;
                    std::cout << "avarage utilizations: " << avarage_utilizations << std::endl;
                    std::cout << "wins: " << wins[alg.first] << std::endl;
                }
            }
            double mixed_avarage_speedup = 0;
            double mixed_avarage_utilizations = 0;
            for (int i = 0; i < file_cnt; i++) {
                mixed_avarage_speedup += mixed_speedup[i];
                mixed_avarage_utilizations += mixed_utilizations[i];
            }
            mixed_avarage_speedup /= file_cnt;
            mixed_avarage_utilizations /= file_cnt;
            std::cout << "\nmixed strategy avarage speedup: " << mixed_avarage_speedup << std::endl;
            std::cout << "mixed strategy avarage utilizations: " << mixed_avarage_utilizations << std::endl;
        }
    }

}