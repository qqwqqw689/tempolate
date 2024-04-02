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

/**
 * vehicle发送消息给map，更新路口的车辆数量
 */
void sendJunctionUpdate(struct VehicleStruct *vehicle, int messageType)
{
    JunctionMessage msg;
    msg.messageType = messageType;
    msg.junctionId = vehicle->currentJunction->id;

    printf("Sending Junction Update: MessageType=%d, JunctionId=%d\n", msg.messageType, msg.junctionId);

    MPI_Send(&msg, 2, MPI_INT, MAP_ACTOR_RANK, TAG_JUNCTION, MPI_COMM_WORLD);
}

/**
 * map接收vehicle发送的消息，更新路口的车辆数量
 */
void receiveJunctionUpdate(struct JunctionStruct *roadMap)
{
    JunctionMessage msg;
    MPI_Status status;

    MPI_Recv(&msg, 2, MPI_INT, MPI_ANY_SOURCE, TAG_JUNCTION, MPI_COMM_WORLD, &status);

    printf("Received Junction Update: MessageType=%d, JunctionId=%d from Source=%d\n", msg.messageType, msg.junctionId, status.MPI_SOURCE);

    if (msg.messageType == ARRIVE_JUNCTION)
    {
        roadMap[msg.junctionId].num_vehicles++;
        roadMap[msg.junctionId].total_number_vehicles++;
    }
    else if (msg.messageType == LEAVE_JUNCTION)
    {
        roadMap[msg.junctionId].num_vehicles--;
    }
}

/**
 * vehicle发送消息给map，更新道路的车辆数量
 */
void sendRoadUpdate(struct VehicleStruct *vehicle, int messageType)
{
    RoadMessage msg;
    int roadIndex = -1;

    // 查找当前车辆所在道路的索引
    for (int i = 0; i < vehicle->currentJunction->num_roads; i++)
    {
        if (&vehicle->currentJunction->roads[i] == vehicle->roadOn)
        {
            roadIndex = i;
            break;
        }
    }

    // 确保成功找到道路索引
    if (roadIndex != -1)
    {
        msg.messageType = messageType;
        msg.junctionId = vehicle->currentJunction->id;
        msg.roadId = roadIndex;

        MPI_Send(&msg, 3, MPI_INT, MAP_ACTOR_RANK, TAG_ROAD, MPI_COMM_WORLD);
    }
    else
    {
        fprintf(stderr, "Error: Unable to find roadIndex for the vehicle on the road.\n");
    }
}

/**
 * map接收vehicle发送的消息，更新道路的车辆数量
 */
void receiveRoadUpdate(struct JunctionStruct *roadMap)
{
    RoadMessage msg;
    MPI_Status status;

    MPI_Recv(&msg, 3, MPI_INT, MPI_ANY_SOURCE, TAG_ROAD, MPI_COMM_WORLD, &status);

    if (msg.messageType == ARRIVE_ROAD)
    {
        roadMap[msg.junctionId].roads[msg.roadId].numVehiclesOnRoad++;
        roadMap[msg.junctionId].roads[msg.roadId].total_number_vehicles++;
        if (roadMap[msg.junctionId].roads[msg.roadId].numVehiclesOnRoad > roadMap[msg.junctionId].roads[msg.roadId].max_concurrent_vehicles)
        {
            roadMap[msg.junctionId].roads[msg.roadId].max_concurrent_vehicles = roadMap[msg.junctionId].roads[msg.roadId].numVehiclesOnRoad;
        }
    }
    else if (msg.messageType == LEAVE_ROAD)
    {
        roadMap[msg.junctionId].roads[msg.roadId].numVehiclesOnRoad--;
    }
}

/**
 * vehicle发送消息给control，更新统计信息
 */
void sendControlMessage(struct VehicleStruct *vehicle, int messageType)
{
    ControlMessage msg;
    msg.messageType = messageType;
    msg.passengers = vehicle->passengers;

    MPI_Send(&msg, 2, MPI_INT, CONTROL_ACTOR_RANK, TAG_STATISITIC, MPI_COMM_WORLD);
}

/**
 * control接收vehicle发送的消息，更新统计信息
 */
void receiveControlMessage(int *total_vehicles, int *passengers_delivered, int *passengers_stranded, int *vehicles_crashed, int *vehicles_exhausted_fuel)
{
    MPI_Status status;
    ControlMessage msg;

    MPI_Recv(&msg, 2, MPI_INT, MPI_ANY_SOURCE, TAG_STATISITIC, MPI_COMM_WORLD, &status);

    if (msg.messageType == NO_FUEL)
    {
        (*vehicles_exhausted_fuel)++;
        (*passengers_stranded) += msg.passengers;
    }
    else if (msg.messageType == VEHICLE_COLLISION)
    {
        (*vehicles_crashed)++;
        (passengers_stranded) += msg.passengers;
    }
    else if (msg.messageType == ARRIVE_DESTINATION)
    {
        (*passengers_delivered) += msg.passengers;
    }
    else if (msg.messageType == NEW_VEHICLE)
    {
        (*total_vehicles)++;
    }
}

/**
 * vehicle请求map进程获取所在节点所有道路的速度
 */
void requestRoadSpeeds(struct VehicleStruct *vehicle, int numRoads, int *speeds)
{
    RequestMessage reqMsg;
    reqMsg.messageType = REQUEST_ROAD_SPEED;
    reqMsg.junctionId = vehicle->currentJunction->id;

    printf("Requesting Road Speeds: JunctionId=%d\n", reqMsg.junctionId);

    MPI_Send(&reqMsg, 2, MPI_INT, MAP_ACTOR_RANK, TAG_REQUEST_ROAD_SPEED, MPI_COMM_WORLD);

    MPI_Status status;
    MPI_Recv(speeds, numRoads, MPI_INT, MAP_ACTOR_RANK, TAG_REQUEST_ROAD_SPEED, MPI_COMM_WORLD, &status);

    printf("Received Road Speeds for JunctionId=%d\n", reqMsg.junctionId);
}

/**
 * map接收vehicle发送的消息，返回所在节点所有道路的速度
 */
void handleRoadSpeedRequest(struct JunctionStruct *roadMap)
{
    RequestMessage reqMsg;
    MPI_Status status;

    MPI_Recv(&reqMsg, 2, MPI_INT, MPI_ANY_SOURCE, TAG_REQUEST_ROAD_SPEED, MPI_COMM_WORLD, &status);

    int numRoads = roadMap[reqMsg.junctionId].num_roads;
    int *speeds = (int *)malloc(numRoads * sizeof(int));

    for (int i = 0; i < numRoads; i++)
    {
        speeds[i] = roadMap[reqMsg.junctionId].roads[i].currentSpeed;
    }

    MPI_Send(speeds, numRoads, MPI_INT, status.MPI_SOURCE, TAG_REQUEST_ROAD_SPEED, MPI_COMM_WORLD);

    free(speeds);
}

/**
 * vehicle请求map进程获取所在节点的信息（仅获取一个int的信息）
 */
void requestJunctionInfo(struct VehicleStruct *vehicle, int messageType, int *info)
{
    RequestMessage reqMsg;
    reqMsg.messageType = messageType;
    reqMsg.junctionId = vehicle->currentJunction->id;

    MPI_Send(&reqMsg, 2, MPI_INT, MAP_ACTOR_RANK, TAG_REQUEST_INFO, MPI_COMM_WORLD);

    MPI_Status status;
    MPI_Recv(info, 1, MPI_INT, MAP_ACTOR_RANK, TAG_REQUEST_INFO, MPI_COMM_WORLD, &status);
}

/**
 * map接收vehicle发送的消息，返回所在节点的信息（仅返回一个int的信息）
 */
void handleJunctionInfoRequest(struct JunctionStruct *roadMap)
{
    RequestMessage reqMsg;
    MPI_Status status;

    MPI_Recv(&reqMsg, 2, MPI_INT, MPI_ANY_SOURCE, TAG_REQUEST_INFO, MPI_COMM_WORLD, &status);

    if (reqMsg.messageType == REQUEST_JUNCTION_NUM_VEHICLES)
    {
        MPI_Send(&roadMap[reqMsg.junctionId].num_vehicles, 1, MPI_INT, status.MPI_SOURCE, TAG_REQUEST_INFO, MPI_COMM_WORLD);
    }
    else if (reqMsg.messageType == REQUEST_AVAILABLE_ROAD)
    {
        MPI_Send(&roadMap[reqMsg.junctionId].trafficLightsRoadEnabled, 1, MPI_INT, status.MPI_SOURCE, TAG_REQUEST_INFO, MPI_COMM_WORLD);
    }
}
