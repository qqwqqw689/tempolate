#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>
#include "pool.h"
#include "mpi.h"
#include "code.h"
#include "comm.h"
#include "function.h"
#include "worker.h"

void printJunctionInfo(struct JunctionStruct *roadMap, int num_junctions)
{
    for (int i = 0; i < num_junctions; i++)
    {
        printf("Junction %d:\n", roadMap[i].id);
        printf("Number of roads: %d\n", roadMap[i].num_roads);
        printf("Number of vehicles: %d\n", roadMap[i].num_vehicles);
        printf("Has traffic lights: %s\n", roadMap[i].hasTrafficLights ? "Yes" : "No");
        printf("Traffic lights road enabled: %d\n", roadMap[i].trafficLightsRoadEnabled);
        printf("Total number of crashes: %d\n", roadMap[i].total_number_crashes);
        printf("Total number of vehicles: %d\n", roadMap[i].total_number_vehicles);
        printf("\n\n");

        printf("Roads:\n");
        for (int j = 0; j < roadMap[i].num_roads; j++)
        {
            printf("Road %d:\n", j);
            printf("From Junction: %d\n", roadMap[i].roads[j].from->id);
            printf("To Junction: %d\n", roadMap[i].roads[j].to->id);
            printf("Road Length: %d\n", roadMap[i].roads[j].roadLength);
            printf("Max Speed: %d\n", roadMap[i].roads[j].maxSpeed);
            printf("Number of Vehicles on Road: %d\n", roadMap[i].roads[j].numVehiclesOnRoad);
            printf("Current Speed: %d\n", roadMap[i].roads[j].currentSpeed);
            printf("Total number of vehicles: %d\n", roadMap[i].roads[j].total_number_vehicles);
            printf("Max concurrent vehicles: %d\n", roadMap[i].roads[j].max_concurrent_vehicles);
            printf("\n\n");
        }
    }
}

static void vehicle(char *);
static void workerCode(char *);
static void map(char *);
static void control();

int main(int argc, char *argv[])
{
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc != 2)
    {
        fprintf(stderr, "Error: You need to provide the roadmap file as the only argument\n");
        exit(-1);
    }
    srand(time(0));

    int statusCode = processPoolInit();
    if (statusCode == 1)
    {
        workerCode(argv[1]);
    }
    else if (statusCode == 2)
    {
        createInitialActor(0); // CONTROL_ACTOR_RANK
        createInitialActor(1); // MAP_ACTOR_RANK
        for (int i = 0; i < INITIAL_VEHICLES; i++)
        {
            createInitialActor(2); // VEHICLE_ACTOR_RANK
        }
        printf("Initial actors created\n");

        int masterStatus = masterPoll();
        while (masterStatus)
        {
            masterStatus = masterPoll();
        }
    }

    processPoolFinalise();
    MPI_Finalize();
    return 0;
}

static void workerCode(char *filename)
{
    int workerStatus = 1, data[1];
    while (workerStatus)
    {
        int parentId = getCommandData();
        // 工作进程从创建它的进程接收data，从而知道自己是哪个actor
        MPI_Recv(data, 1, MPI_INT, parentId, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        if (data[0] == 0)
        {
            control();
        }
        else if (data[0] == 1)
        {
            map(filename);
        }
        else if (data[0] == 2)
        {
            vehicle(filename);
        }
        workerStatus = workerSleep();
    }
}

/*
 * control演员需要从vehicle演员处接收消息，进行统计和打印
 */
static void control()
{
    int elapsed_mins = 0;
    time_t start_seconds = getCurrentSeconds();
    time_t seconds = 0;
    int total_vehicles = INITIAL_VEHICLES;
    int passengers_delivered = 0;
    int passengers_stranded = 0;
    int vehicles_crashed = 0;
    int vehicles_exhausted_fuel = 0;
    while (1 == 1)
    {
        time_t current_seconds = getCurrentSeconds();

        if (current_seconds != seconds)
        {
            seconds = current_seconds;
            // 每过一秒，检查是否输出状态
            if (seconds - start_seconds > 0)
            {
                // 每过 MIN_LENGTH_SECONDS 秒，输出一次状态
                if ((seconds - start_seconds) % MIN_LENGTH_SECONDS == 0)
                {
                    // 每 MIN_LENGTH_SECONDS 秒意味着模拟时间过了一分钟
                    elapsed_mins++;
                    // 每分钟随机生成 100-200 辆车
                    int num_new_vehicles = getRandomInteger(100, 200);
                    for (int i = 0; i < num_new_vehicles; i++)
                    {
                        int workerPid = startWorkerProcess();
                        int new_ac_data = 3;
                        MPI_Bsend(&new_ac_data, 1, MPI_INT, workerPid, 0, MPI_COMM_WORLD);
                    }
                    total_vehicles += num_new_vehicles;
                    // 每 SUMMARY_FREQUENCY 分钟输出一次状态
                    if (elapsed_mins % SUMMARY_FREQUENCY == 0)
                    {
                        printf("[Time: %d mins] %d vehicles, %d passengers delivered, %d stranded passengers, %d crashed vehicles, %d vehicles exhausted fuel\n",
                               elapsed_mins, total_vehicles, passengers_delivered, passengers_stranded, vehicles_crashed, vehicles_exhausted_fuel);
                    }
                }
            }
        }

        /*
         * 接收消息，进行对应处理
         */
        int flag;
        MPI_Status status;
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
        if (flag)
        {
            if (status.MPI_TAG == TAG_STATISITIC)
            {
                receiveControlMessage(&total_vehicles, &passengers_delivered, &passengers_stranded, &vehicles_crashed, &vehicles_exhausted_fuel);
            }
        }

        /*
         * 检查是否模拟结束
         */
        if (elapsed_mins >= MAX_MINS)
        {
            shutdownPool();
            break;
        }
    }
}

static void map(char *filename)
{
    int elapsed_mins = 0;
    time_t start_seconds = getCurrentSeconds();
    time_t seconds = 0;

    /*
     * 初始化地图
     */
    struct JunctionStruct *roadMap = NULL;
    int num_junctions, num_roads = 0;
    loadRoadMap(filename, &roadMap, &num_junctions, &num_roads);
    // printf("Loaded road map from file\n");
    // printJunctionInfo(roadMap, num_junctions);

    while (1 == 1)
    {
        /*
         * 检测是否应该停止
         */
        if (shouldWorkerStop())
            break;

        /*
         * 更新分钟数
         */
        time_t current_seconds = getCurrentSeconds();
        if (current_seconds != seconds)
        {
            seconds = current_seconds;
            if (seconds - start_seconds > 0)
            {
                if ((seconds - start_seconds) % MIN_LENGTH_SECONDS == 0)
                {
                    elapsed_mins++;
                }
            }
        }

        /*
         * 更新信号灯和所有道路的限速
         */
        for (int i = 0; i < num_junctions; i++)
        {

            // 更新信号灯
            if (roadMap[i].hasTrafficLights && roadMap[i].num_roads > 0)
            {
                roadMap[i].trafficLightsRoadEnabled = elapsed_mins % roadMap[i].num_roads;
            }

            // 更新道路的限速
            for (int j = 0; j < roadMap[i].num_roads; j++)
            {
                struct RoadStruct *road = &roadMap[i].roads[j];
                road->currentSpeed = road->maxSpeed - road->numVehiclesOnRoad;
                if (road->currentSpeed < 10)
                    road->currentSpeed = 10;
            }
        }

        /*
         * 接收消息，进行对应的处理
         */
        int flag;
        MPI_Status status;
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);
        if (flag)
        {
            if (status.MPI_TAG == TAG_JUNCTION)
            {
                receiveJunctionUpdate(roadMap);
            }
            else if (status.MPI_TAG == TAG_ROAD)
            {
                receiveRoadUpdate(roadMap);
            }
            else if (status.MPI_TAG == TAG_REQUEST_ROAD_SPEED)
            {
                handleRoadSpeedRequest(roadMap);
            }
            else if (status.MPI_TAG == TAG_REQUEST_INFO)
            {
                handleJunctionInfoRequest(roadMap);
            }
        }
    }
}

/*
 * vehicle演员
 */
static void vehicle(char *filename)
{
    /*
     * 初始化vehicle内部的静态地图
     */
    struct JunctionStruct *roadMap = NULL;
    int num_junctions, num_roads = 0;
    loadRoadMap(filename, &roadMap, &num_junctions, &num_roads);
    // printf("Loaded road map from file\n");
    // printJunctionInfo(roadMap, num_junctions);

    /*
     * 对vehicle进行初始化
     */
    struct VehicleStruct vehicle;
    activateRandomVehicle(&vehicle, num_junctions, roadMap);
    // printf("Vehicle activated\n");

    // 给map发送消息更新vehicle所在的路口的车辆数量
    sendJunctionUpdate(&vehicle, ARRIVE_JUNCTION);

    while (1 == 1)
    {
        /*
         * 检测是否应该停止
         */
        if (shouldWorkerStop())
            break;

        /*
         * 检查燃料是否耗尽
         */
        if (getCurrentSeconds() - vehicle.start_t > vehicle.fuel)
        {
            // 发送统计信息
            sendControlMessage(&vehicle, NO_FUEL);

            // 发送消息给map，更新对应的位置的计数
            if (vehicle.roadOn != NULL)
            {
                sendRoadUpdate(&vehicle, LEAVE_ROAD);
            }
            if (vehicle.currentJunction != NULL)
            {
                sendJunctionUpdate(&vehicle, LEAVE_JUNCTION);
            }

            // 跳出循环
            break;
        }

        /*
         * 如果车辆在道路上且不在路口上，判断是否移动车辆到下一个路口
         */
        if (vehicle.roadOn != NULL && vehicle.currentJunction == NULL)
        {
            // 如果时间不足一秒，跳过后续所有计算
            time_t sec = getCurrentSeconds();
            int latest_time = sec - vehicle.last_distance_check_secs;
            if (latest_time < 1)
                continue;

            // 更新最后一次被检查的时间
            vehicle.last_distance_check_secs = sec;

            // 更新到下一个路口的距离
            double travelled_length = latest_time * vehicle.speed;
            vehicle.remaining_distance -= travelled_length;

            // 判断车辆是否到达下一个路口，如果是则移动车辆到下一个路口
            if (vehicle.remaining_distance <= 0)
            {
                // 发送消息给map，更新对应的位置的计数
                sendRoadUpdate(&vehicle, LEAVE_ROAD);
                sendJunctionUpdate(&vehicle, ARRIVE_JUNCTION);

                // 更新车辆的位置
                vehicle.roadOn = NULL;
                vehicle.currentJunction = vehicle.roadOn->to;

                // 更新其他信息
                vehicle.remaining_distance = 0;
                vehicle.arrived_road_time = 0;
                vehicle.speed = 0;
                vehicle.last_distance_check_secs = 0;
            }
        }

        /*
         * 如果车辆在路口上且不在道路上
         */
        if (vehicle.currentJunction != NULL && vehicle.roadOn == NULL)
        {
            /*
             * 判断是否到达目的地
             */
            if (vehicle.currentJunction->id == vehicle.dest)
            {
                // 发送统计信息
                sendControlMessage(&vehicle, ARRIVE_DESTINATION);

                // 发送消息给map，更新对应的位置的计数
                sendJunctionUpdate(&vehicle, LEAVE_JUNCTION);

                // 跳出循环
                break;
            }

            /*
             * 规划路线，寻找下一个道路
             */
            // 向map发送消息，请求所在路口的所有道路的速度
            int numRoads = vehicle.currentJunction->num_roads;
            int *speeds = (int *)malloc(numRoads * sizeof(int));
            requestRoadSpeeds(&vehicle, numRoads, speeds);

            // 更新本地地图对应的道路的当前速度
            for (int i = 0; i < numRoads; i++)
            {
                vehicle.currentJunction->roads[i].currentSpeed = speeds[i];
            }
            free(speeds);

            // 规划路线
            int next_junction_target = planRoute(vehicle.source, vehicle.dest, num_junctions, roadMap);
            int road_to_take = findAppropriateRoad(next_junction_target, vehicle.currentJunction);
            assert(vehicle.currentJunction->roads[road_to_take].to->id == next_junction_target);

            /*
             * 移动车辆到目标道路上
             */
            vehicle.roadOn = &vehicle.currentJunction->roads[road_to_take];

            // 发送消息给map，更新对应的位置的计数
            sendRoadUpdate(&vehicle, ARRIVE_ROAD);

            // 更新车辆的其他信息
            vehicle.remaining_distance = vehicle.roadOn->roadLength;
            vehicle.speed = vehicle.roadOn->currentSpeed;
            if (vehicle.speed > vehicle.maxSpeed)
            {
                vehicle.speed = vehicle.maxSpeed;
            }
        }

        /*
         * 如果车辆的道路和路口都不为空，判断车辆是否能从路口释放
         */
        if (vehicle.roadOn != NULL && vehicle.currentJunction != NULL)
        {
            char take_road = 0;

            /*
             * 如果路口有信号灯，仅当信号灯允许时，车辆才能通过路口
             */
            if (vehicle.currentJunction->hasTrafficLights)
            {
                // 判断信号灯是否允许通过
                int trafficLightsRoadEnabled;
                requestJunctionInfo(&vehicle, REQUEST_AVAILABLE_ROAD, &trafficLightsRoadEnabled);
                take_road = vehicle.roadOn == &vehicle.currentJunction->roads[trafficLightsRoadEnabled];
            }

            /*
             * 如果没有信号灯，判断是否发生碰撞事件，如果没有则车辆可以通过路口
             */
            else
            {
                // 计算碰撞概率
                int num_vehicles;
                requestJunctionInfo(&vehicle, REQUEST_JUNCTION_NUM_VEHICLES, &num_vehicles);
                int collision = getRandomInteger(0, 8) * num_vehicles;

                // 如果发生碰撞，车辆移除
                if (collision > 40)
                {
                    // 发送统计信息
                    sendControlMessage(&vehicle, VEHICLE_COLLISION);

                    // 发送消息给map，更新对应的位置的计数
                    if (vehicle.roadOn != NULL)
                    {
                        sendRoadUpdate(&vehicle, LEAVE_ROAD);
                    }
                    if (vehicle.currentJunction != NULL)
                    {
                        sendJunctionUpdate(&vehicle, LEAVE_JUNCTION);
                    }

                    // 跳出循环
                    break;
                }

                // 车辆可以通过路口
                take_road = 1;
            }

            if (take_road)
            {
                // 发送消息给map，更新对应的位置的计数
                sendJunctionUpdate(&vehicle, LEAVE_JUNCTION);

                // 更新车辆的其他信息
                vehicle.currentJunction = NULL;
                vehicle.last_distance_check_secs = getCurrentSeconds();
            }
        }
    }
}
