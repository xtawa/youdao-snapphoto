#include "../native/rknn/npu_bridge.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>
int main(int argc,char**argv){
 if(argc!=6){std::cerr<<"Usage: probe model input.rgb output.rgb sigma iterations\n";return 2;}
 std::vector<unsigned char> rgb(256*256*3),out(rgb.size());
 std::ifstream input(argv[2],std::ios::binary);if(!input.read(reinterpret_cast<char*>(rgb.data()),rgb.size()))return 3;
 SnapNpuInfo info{};
 if(!snap_npu_open(argv[1],1,&info)){std::cerr<<info.error<<'\n';return 4;}
 auto began=std::chrono::steady_clock::now();int count=std::atoi(argv[5]);if(count<1||count>100)return 2;
 for(int i=0;i<count;i++)if(!snap_npu_tile(rgb.data(),256*3,std::atoi(argv[4]),out.data(),&info)){std::cerr<<info.error<<'\n';snap_npu_close();return 5;}
 double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
 std::ofstream output(argv[3],std::ios::binary);output.write(reinterpret_cast<char*>(out.data()),out.size());output.close();
 std::ofstream perf(std::string(argv[3])+".perf.txt");perf<<snap_npu_performance();
 std::cout<<"{\"sdk\":"<<std::quoted(info.sdk)<<",\"driver\":"<<std::quoted(info.driver)<<",\"iterations\":"<<count
          <<",\"averageMs\":"<<elapsed/count<<",\"lastRunUs\":"<<info.last_run_us<<",\"weightBytes\":"<<info.weight_bytes
          <<",\"internalBytes\":"<<info.internal_bytes<<",\"dmaBytes\":"<<info.dma_bytes<<",\"inputFormat\":"<<info.input_format
          <<",\"outputFormat\":"<<info.output_format<<",\"inputType\":"<<info.input_type<<",\"outputType\":"<<info.output_type<<"}\n";
 snap_npu_close();return output?0:6;
}
