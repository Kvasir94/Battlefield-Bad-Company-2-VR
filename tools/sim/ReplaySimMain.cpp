// fvr_replay_sim: offline BC2 replay simulator CLI (docs/REPLAY_INPUT.md).
//   fvr_replay_sim run --tape t.fvrtape --events gun-events.sim.jsonl [--summary s.json]
//                      [--loaded 27 --reserve 83 --capacity 30 --tail 30]
//   fvr_replay_sim synth-xm8 --out t.fvrtape [--carry | --slider-release] [--events e.jsonl]
// Exit: 0 ran (verdicts belong to tools/replay), 2 usage/IO/tape error, 3 precondition unmet,
// 4 synthetic generation failed.
#include "Bc2ReplaySim.h"
#include <fstream>
#include <iostream>
#include <string>
namespace {
int Usage(){std::cerr<<"Usage: fvr_replay_sim run --tape PATH --events PATH [--summary PATH] [--loaded N --reserve N --capacity N --tail N]\n"
 "       fvr_replay_sim synth-xm8 --out PATH [--carry | --slider-release] [--events PATH]\n";return 2;}
}
int main(int argc,char** argv){try{
 if(argc<2)return Usage();const std::string command=argv[1];
 std::string tapePath,eventsPath,summaryPath,outPath;bool carry=false,sliderRelease=false;fvr::sim::SimOptions options;
 for(int i=2;i<argc;++i){const std::string a=argv[i];auto value=[&]()->std::string{if(i+1>=argc)throw std::runtime_error("missing value for "+a);return argv[++i];};
  if(a=="--tape")tapePath=value();else if(a=="--events")eventsPath=value();else if(a=="--summary")summaryPath=value();else if(a=="--out")outPath=value();
  else if(a=="--carry")carry=true;else if(a=="--slider-release")sliderRelease=true;else if(a=="--loaded")options.loaded=std::stoi(value());else if(a=="--reserve")options.reserve=std::stoi(value());
  else if(a=="--capacity")options.capacity=std::stoi(value());else if(a=="--tail")options.tailTicks=unsigned(std::stoul(value()));
  else return Usage();}
 if(options.capacity<=0||options.loaded<0||options.loaded>options.capacity||options.reserve<0)throw std::runtime_error("invalid start ammunition");
 std::ofstream events;if(!eventsPath.empty()){events.open(eventsPath,std::ios::binary);if(!events)throw std::runtime_error("cannot open events output");}
 const fvr::sim::EventSink sink=[&](const std::string& line){if(events)events<<line<<'\n';};
 fvr::sim::SimSummary summary;int code=0;
 if(command=="run"){
  if(tapePath.empty()||eventsPath.empty())return Usage();
  std::ifstream in(tapePath,std::ios::binary);if(!in)throw std::runtime_error("cannot open tape");
  fvr::runtime::InputTape tape;std::string error;if(!tape.Load(in,error)){std::cerr<<"tape rejected: "<<error<<'\n';return 2;}
  code=fvr::sim::RunTape(tape,options,sink,summary)?0:3;
 }else if(command=="synth-xm8"){
  if(outPath.empty())return Usage();std::ofstream out(outPath,std::ios::binary);if(!out)throw std::runtime_error("cannot open tape output");
  if(carry&&sliderRelease)return Usage();
  code=fvr::sim::GenerateXm8MagazineTape(out,carry,summary,sink,sliderRelease)?0:4;
 }else return Usage();
 events.flush();if(events.is_open()&&!events)throw std::runtime_error("event write failed");
 if(!summaryPath.empty()){std::ofstream s(summaryPath,std::ios::binary);fvr::sim::WriteSummary(s,summary);if(!s)throw std::runtime_error("summary write failed");}
 fvr::sim::WriteSummary(std::cout,summary);return code;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
