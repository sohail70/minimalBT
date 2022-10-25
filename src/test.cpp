#include<iostream>
#include<chrono>
#include<thread>
#include<cmath>
using namespace std::chrono_literals;

int main()
{

    std::chrono::time_point<std::chrono::system_clock , std::chrono::system_clock::duration> time{0s};
    
    std::chrono::milliseconds dur = 1ms;
    time =time + dur;

        std::cout<<time.time_since_epoch().count()<<"\n";
}