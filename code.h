
#define VERBOSE_ROUTE_PLANNER 0
#define LARGE_NUM 99999999.0

#define MAX_ROAD_LEN 100
#define MAX_VEHICLES 4
#define MAX_MINS 10
#define MIN_LENGTH_SECONDS 2
#define MAX_NUM_ROADS_PER_JUNCTION 50
#define SUMMARY_FREQUENCY 5
#define INITIAL_VEHICLES 1

#define BUS_PASSENGERS 80
#define BUS_MAX_SPEED 50
#define BUS_MIN_FUEL 10
#define BUS_MAX_FUEL 100
#define CAR_PASSENGERS 4
#define CAR_MAX_SPEED 100
#define CAR_MIN_FUEL 1
#define CAR_MAX_FUEL 40
#define MINI_BUS_MAX_SPEED 80
#define MINI_BUS_PASSENGERS 15
#define MINI_BUS_MIN_FUEL 2
#define MINI_BUS_MAX_FUEL 75
#define COACH_MAX_SPEED 60
#define COACH_PASSENGERS 40
#define COACH_MIN_FUEL 20
#define COACH_MAX_FUEL 200
#define MOTOR_BIKE_MAX_SPEED 120
#define MOTOR_BIKE_PASSENGERS 2
#define MOTOR_BIKE_MIN_FUEL 1
#define MOTOR_BIKE_MAX_FUEL 20
#define BIKE_MAX_SPEED 10
#define BIKE_PASSENGERS 1
#define BIKE_MIN_FUEL 2
#define BIKE_MAX_FUEL 10

#define CONTROL_ACTOR_RANK 1
#define MAP_ACTOR_RANK 2

enum ReadMode
{
    NONE,
    ROADMAP,
    TRAFFICLIGHTS
};

enum VehicleType
{
    CAR,
    BUS,
    MINI_BUS,
    COACH,
    MOTORBIKE,
    BIKE
};

struct JunctionStruct
{
    int id, num_roads, num_vehicles;
    char hasTrafficLights;
    int trafficLightsRoadEnabled;
    int total_number_crashes, total_number_vehicles;
    struct RoadStruct *roads;
};

struct RoadStruct
{
    struct JunctionStruct *from, *to;
    int roadLength, maxSpeed, numVehiclesOnRoad, currentSpeed;
    int total_number_vehicles, max_concurrent_vehicles;
};

struct VehicleStruct
{
    int passengers, source, dest, maxSpeed;
    // Distance is in meters
    int speed, arrived_road_time, fuel;
    time_t last_distance_check_secs, start_t;
    double remaining_distance;
    char active;
    struct JunctionStruct *currentJunction;
    struct RoadStruct *roadOn;
};

