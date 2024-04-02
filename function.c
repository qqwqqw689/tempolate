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
 * Parses the provided roadmap file and uses this to build the graph of
 * junctions and roads, as well as reading traffic light information
 **/
void loadRoadMap(char *filename, struct JunctionStruct **roadMap, int *num_junctions, int *num_roads)
{
    enum ReadMode currentMode = NONE;
    char buffer[MAX_ROAD_LEN];
    FILE *f = fopen(filename, "r");
    if (f == NULL)
    {
        fprintf(stderr, "Error opening roadmap file '%s'\n", filename);
        exit(-1);
    }

    *num_junctions = 0; // 确保开始时交叉口数量为0
    *num_roads = 0;     // 确保开始时道路数量为0

    while (fgets(buffer, MAX_ROAD_LEN, f))
    {
        if (buffer[0] == '%')
            continue;
        if (buffer[0] == '#')
        {
            if (strncmp("# Road layout:", buffer, 14) == 0)
            {
                char *s = strstr(buffer, ":");
                *num_junctions = atoi(&s[1]);
                *roadMap = (struct JunctionStruct *)malloc(sizeof(struct JunctionStruct) * *num_junctions);
                for (int i = 0; i < *num_junctions; i++)
                {
                    (*roadMap)[i].id = i;
                    (*roadMap)[i].num_roads = 0;
                    (*roadMap)[i].num_vehicles = 0;
                    (*roadMap)[i].hasTrafficLights = 0;
                    (*roadMap)[i].total_number_crashes = 0;
                    (*roadMap)[i].total_number_vehicles = 0;
                    // Not ideal to allocate all roads size here
                    (*roadMap)[i].roads = (struct RoadStruct *)malloc(sizeof(struct RoadStruct) * MAX_NUM_ROADS_PER_JUNCTION);
                }
                currentMode = ROADMAP;
            }
            if (strncmp("# Traffic lights:", buffer, 17) == 0)
            {
                currentMode = TRAFFICLIGHTS;
            }
        }
        else
        {
            if (currentMode == ROADMAP)
            {
                char *space = strstr(buffer, " ");
                *space = '\0';
                int from_id = atoi(buffer);
                char *nextspace = strstr(&space[1], " ");
                *nextspace = '\0';
                int to_id = atoi(&space[1]);
                char *nextspace2 = strstr(&nextspace[1], " ");
                *nextspace = '\0';
                int roadlength = atoi(&nextspace[1]);
                int speed = atoi(&nextspace2[1]);
                if ((*roadMap)[from_id].num_roads >= MAX_NUM_ROADS_PER_JUNCTION)
                {
                    fprintf(stderr, "Error: Tried to create road %d at junction %d, but maximum number of roads is %d, increase 'MAX_NUM_ROADS_PER_JUNCTION'",
                            (*roadMap)[from_id].num_roads, from_id, MAX_NUM_ROADS_PER_JUNCTION);
                    exit(-1);
                }
                (*roadMap)[from_id].roads[(*roadMap)[from_id].num_roads].from = &(*roadMap)[from_id];
                (*roadMap)[from_id].roads[(*roadMap)[from_id].num_roads].to = &(*roadMap)[to_id];
                (*roadMap)[from_id].roads[(*roadMap)[from_id].num_roads].roadLength = roadlength;
                (*roadMap)[from_id].roads[(*roadMap)[from_id].num_roads].maxSpeed = speed;
                (*roadMap)[from_id].roads[(*roadMap)[from_id].num_roads].numVehiclesOnRoad = 0;
                (*roadMap)[from_id].roads[(*roadMap)[from_id].num_roads].currentSpeed = speed;
                (*roadMap)[from_id].roads[(*roadMap)[from_id].num_roads].total_number_vehicles = 0;
                (*roadMap)[from_id].roads[(*roadMap)[from_id].num_roads].max_concurrent_vehicles = 0;
                (*roadMap)[from_id].num_roads++;
                (*num_roads)++;
            }
            else if (currentMode == TRAFFICLIGHTS)
            {
                int id = atoi(buffer);
                if ((*roadMap)[id].num_roads > 0)
                    (*roadMap)[id].hasTrafficLights = 1;
            }
        }
    }
    fclose(f);
}

/**
 * Plans a route from the source to destination junction, returning the junction after
 * the source junction. This will be called to plan a route from A (where the vehicle
 * is currently) to B (the destination), so will return the junction that most be travelled
 * to next. -1 is returned if no route is found
 **/
int planRoute(int source_id, int dest_id, int num_junctions, struct JunctionStruct *roadMap)
{
    if (VERBOSE_ROUTE_PLANNER)
        printf("Search for route from %d to %d\n", source_id, dest_id);
    double *dist = (double *)malloc(sizeof(double) * num_junctions);
    char *active = (char *)malloc(sizeof(char) * num_junctions);
    struct JunctionStruct **prev = (struct JunctionStruct **)malloc(sizeof(struct JunctionStruct *) * num_junctions);

    int activeJunctions = num_junctions;
    for (int i = 0; i < num_junctions; i++)
    {
        active[i] = 1;
        prev[i] = NULL;
        if (i != source_id)
        {
            dist[i] = LARGE_NUM;
        }
    }
    dist[source_id] = 0;
    while (activeJunctions > 0)
    {
        int v_idx = findIndexOfMinimum(dist, active, num_junctions);
        if (v_idx == dest_id)
            break;
        struct JunctionStruct *v = &roadMap[v_idx];
        active[v_idx] = 0;
        activeJunctions--;

        for (int i = 0; i < v->num_roads; i++)
        {
            if (active[v->roads[i].to->id] && dist[v_idx] != LARGE_NUM)
            {
                double alt = dist[v_idx] + v->roads[i].roadLength / (v->id == source_id ? v->roads[i].currentSpeed : v->roads[i].maxSpeed);
                if (alt < dist[v->roads[i].to->id])
                {
                    dist[v->roads[i].to->id] = alt;
                    prev[v->roads[i].to->id] = v;
                }
            }
        }
    }
    free(dist);
    free(active);
    int u_idx = dest_id;
    int *route = (int *)malloc(sizeof(int) * num_junctions);
    int route_len = 0;
    if (prev[u_idx] != NULL || u_idx == source_id)
    {
        if (VERBOSE_ROUTE_PLANNER)
            printf("Start at %d\n", u_idx);
        while (prev[u_idx] != NULL)
        {
            route[route_len] = u_idx;
            u_idx = prev[u_idx]->id;
            if (VERBOSE_ROUTE_PLANNER)
                printf("Route %d\n", u_idx);
            route_len++;
        }
    }
    free(prev);
    if (route_len > 0)
    {
        int next_jnct = route[route_len - 1];
        if (VERBOSE_ROUTE_PLANNER)
            printf("Found next junction is %d\n", next_jnct);
        free(route);
        return next_jnct;
    }
    if (VERBOSE_ROUTE_PLANNER)
        printf("Failed to find route between %d and %d\n", source_id, dest_id);
    free(route);
    return -1;
}

/**
 * Finds the index of the input array that is active and has the smallest number
 **/
int findIndexOfMinimum(double *dist, char *active, int num_junctions)
{
    double min_dist = LARGE_NUM + 1;
    int current_min = -1;
    for (int i = 0; i < num_junctions; i++)
    {
        if (active[i] && dist[i] < min_dist)
        {
            min_dist = dist[i];
            current_min = i;
        }
    }
    return current_min;
}

/**
 * Generates a random integer between two values, including the from value up to the to value minus
 * one, i.e. from=0, to=100 will generate a random integer between 0 and 99 inclusive
 **/
int getRandomInteger(int from, int to)
{
    return (rand() % (to - from)) + from;
}

/**
 * Retrieves the current time in seconds
 **/
time_t getCurrentSeconds()
{
    struct timeval curr_time;
    gettimeofday(&curr_time, NULL);
    time_t current_seconds = curr_time.tv_sec;
    return current_seconds;
}

/**
 * Finds the road index out of the junction's roads that leads to a specific
 * destination junction
 **/
int findAppropriateRoad(int dest_junction, struct JunctionStruct *junction)
{
    // 遍历该路口连接的所有道路，检查每一条道路的终点是否是目的地路口
    for (int j = 0; j < junction->num_roads; j++)
    {
        if (junction->roads[j].to->id == dest_junction)
            return j;
    }
    return -1;
}