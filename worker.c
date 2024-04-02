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
 * Activates a vehicle and sets its type and route randomly
 **/
void activateRandomVehicle(struct VehicleStruct *vehicle, int num_junctions, struct JunctionStruct *roadMap)
{
    int random_vehicle_type = getRandomInteger(0, 5);
    enum VehicleType vehicleType;
    if (random_vehicle_type == 0)
    {
        vehicleType = BUS;
    }
    else if (random_vehicle_type == 1)
    {
        vehicleType = CAR;
    }
    else if (random_vehicle_type == 2)
    {
        vehicleType = MINI_BUS;
    }
    else if (random_vehicle_type == 3)
    {
        vehicleType = COACH;
    }
    else if (random_vehicle_type == 4)
    {
        vehicleType = MOTORBIKE;
    }
    else if (random_vehicle_type == 5)
    {
        vehicleType = BIKE;
    }

    // 设置交通工具的类型
    vehicle->active = 1;
    vehicle->start_t = getCurrentSeconds();
    vehicle->last_distance_check_secs = 0;
    vehicle->speed = 0;
    vehicle->remaining_distance = 0;
    vehicle->arrived_road_time = 0;
    // 设置起始地和目的地
    vehicle->source = vehicle->dest = getRandomInteger(0, num_junctions);
    while (vehicle->dest == vehicle->source)
    {
        // Ensure that the source and destination are different
        vehicle->dest = getRandomInteger(0, num_junctions);
        // 确保从 source 到 dest 有路
        if (vehicle->dest != vehicle->source)
        {
            // See if there is a viable route between the source and destination
            int next_jnct = planRoute(vehicle->source, vehicle->dest, num_junctions, roadMap);
            if (next_jnct == -1)
            {
                // Regenerate source and dest
                vehicle->source = vehicle->dest = getRandomInteger(0, num_junctions);
            }
        }
    }
    // 设置所在路口和道路
    vehicle->currentJunction = &roadMap[vehicle->source];
    vehicle->roadOn = NULL;
    // 设置交通工具的最大速度、乘客数量和燃油
    if (vehicleType == CAR)
    {
        vehicle->maxSpeed = CAR_MAX_SPEED;
        vehicle->passengers = getRandomInteger(1, CAR_PASSENGERS);
        vehicle->fuel = getRandomInteger(CAR_MIN_FUEL, CAR_MAX_FUEL);
    }
    else if (vehicleType == BUS)
    {
        vehicle->maxSpeed = BUS_MAX_SPEED;
        vehicle->passengers = getRandomInteger(1, BUS_PASSENGERS);
        vehicle->fuel = getRandomInteger(BUS_MIN_FUEL, BUS_MAX_FUEL);
    }
    else if (vehicleType == MINI_BUS)
    {
        vehicle->maxSpeed = MINI_BUS_MAX_SPEED;
        vehicle->passengers = getRandomInteger(1, MINI_BUS_PASSENGERS);
        vehicle->fuel = getRandomInteger(MINI_BUS_MIN_FUEL, MINI_BUS_MAX_FUEL);
    }
    else if (vehicleType == COACH)
    {
        vehicle->maxSpeed = COACH_MAX_SPEED;
        vehicle->passengers = getRandomInteger(1, COACH_PASSENGERS);
        vehicle->fuel = getRandomInteger(COACH_MIN_FUEL, COACH_MAX_FUEL);
    }
    else if (vehicleType == MOTORBIKE)
    {
        vehicle->maxSpeed = MOTOR_BIKE_MAX_SPEED;
        vehicle->passengers = getRandomInteger(1, MOTOR_BIKE_PASSENGERS);
        vehicle->fuel = getRandomInteger(MOTOR_BIKE_MIN_FUEL, MOTOR_BIKE_MAX_FUEL);
    }
    else if (vehicleType == BIKE)
    {
        vehicle->maxSpeed = BIKE_MAX_SPEED;
        vehicle->passengers = getRandomInteger(1, BIKE_PASSENGERS);
        vehicle->fuel = getRandomInteger(BIKE_MIN_FUEL, BIKE_MAX_FUEL);
    }
    else
    {
        fprintf(stderr, "Unknown vehicle type\n");
    }

    /**
     * vehicle向control发送统计信息
     */
    sendControlMessage(vehicle, NEW_VEHICLE);
}

void createInitialActor(int type)
{
    int data[1];
    int workerPid = startWorkerProcess();
    data[0] = type;
    MPI_Bsend(data, 1, MPI_INT, workerPid, 0, MPI_COMM_WORLD);
}