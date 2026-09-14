#include "Core/Simulation.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    StatisticsSnapshot RunScenario(const std::string& name)
    {
        SimulationConfig config;
        config.floorCount=20; config.elevatorCount=3; config.capacity=4;
        config.passengerRate=0; config.simulationDuration=200;
        if(name=="finite90" || name=="batch2000")
        {
            config.elevatorCount=6; config.capacity=3;
            config.moveTimePerFloor=0.5; config.personTime=0.25; config.simulationDuration=20000;
        }
        if(name=="poisson321")
        {
            config.elevatorCount=6; config.capacity=4;
            config.moveTimePerFloor=0.3; config.personTime=0.2;
            config.passengerRate=8; config.simulationDuration=600;
        }
        if(name=="evening_peak")
        {
            config.elevatorCount=6; config.capacity=4;
            config.moveTimePerFloor=0.3; config.personTime=0.2; config.simulationDuration=600;
        }
        if(name=="interfloor" || name=="saturation")
        {
            config.elevatorCount=6; config.capacity=name=="saturation" ? 2 : 4;
            config.moveTimePerFloor=0.3; config.personTime=0.2; config.simulationDuration=20000;
        }
        Simulation simulation;
        if(!simulation.Initialize(config,321)) throw std::runtime_error("initialize failed");
        if(name=="two_calls")
        {
            simulation.AddPassenger(9,20); simulation.AddPassenger(11,1);
        }
        else if(name=="new_detour") simulation.AddPassenger(15,20);
        else if(name=="finite90" || name=="batch2000")
        {
            const int count=name=="finite90" ? 90 : 2000;
            for(int i=0;i<count;++i)
            {
                const int start=i%20+1;
                simulation.AddPassenger(start,(start-1+1+i%19)%20+1);
            }
        }
        else if(name=="evening_peak")
        {
            // 晚高峰下行：偶数乘客从上层→1，奇数乘客从 1→上层。
            for(int i=0;i<300;++i)
            {
                const int floor=2+i%19;
                if(i%2==0) simulation.AddPassenger(floor,1);
                else simulation.AddPassenger(1,floor);
            }
        }
        else if(name=="interfloor")
        {
            // 区间双向：在 5~16 层内错位起终点，均衡上行/下行。
            for(int i=0;i<300;++i)
            {
                const int start=5+i%12;
                const int target=(start-4+i%11)%12+5;
                simulation.AddPassenger(start,target);
            }
        }
        else if(name=="saturation")
        {
            // 容量饱和：capacity=2，600 人有限批次，考查高积压下的送达与等待。
            for(int i=0;i<600;++i)
            {
                const int start=i%20+1;
                simulation.AddPassenger(start,(start-1+1+i%19)%20+1);
            }
        }
        simulation.Start();
        if(name=="new_detour")
        {
            simulation.Update(2); simulation.AddPassenger(17,1);
        }
        simulation.Update(config.simulationDuration);
        if(!simulation.ValidateState()) throw std::runtime_error("population or ownership mismatch");
        return simulation.GetStatisticsSnapshot();
    }
}

int main()
{
    try
    {
        std::cout << "scenario,total,arrived,waiting,riding,mean_wait_s,completed_pickup_delay_s,elapsed_ms\n";
        for(const std::string name:{"two_calls","new_detour","finite90","batch2000","poisson321",
            "evening_peak","interfloor","saturation"})
        {
            StatisticsSnapshot stats;
            const auto begin=std::chrono::steady_clock::now();
            constexpr int repeats=3;
            for(int run=0;run<repeats;++run) stats=RunScenario(name);
            const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/repeats;
            const double personTime=name=="finite90" || name=="batch2000" ? 0.25 :
                (name=="poisson321" || name=="evening_peak" || name=="interfloor" || name=="saturation" ? 0.2 : 3.0);
            // 已完成上梯者的响应延迟总和，扣掉其自身上梯 T；积压另列，不能当作全体均值。
            const double response=(stats.averageWaitingTime-personTime)*stats.boardedCount;
            std::cout << std::fixed << std::setprecision(4) << name << ',' << stats.totalPassengerCount << ','
                << stats.arrivedCount << ',' << stats.waitingCount << ',' << stats.ridingCount << ','
                << stats.averageWaitingTime << ',' << response << ',' << elapsed << '\n';
        }
    }
    catch(const std::exception& error)
    {
        std::cerr << error.what() << '\n'; return 1;
    }
    return 0;
}
