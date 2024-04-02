#define NO_FUEL 0
#define LEAVE_ROAD 1
#define ARRIVE_JUNCTION 2
#define ARRIVE_DESTINATION 3
#define ARRIVE_ROAD 4
#define LEAVE_JUNCTION 5
#define REQUEST_ROAD_SPEED 6
#define VEHICLE_COLLISION 7
#define REQUEST_AVAILABLE_ROAD 8
#define REQUEST_JUNCTION_NUM_VEHICLES 9
#define NEW_VEHICLE 10
#define STOP_SIGNAL 99

#define TAG_JUNCTION 1
#define TAG_ROAD 2
#define TAG_REQUEST_ROAD_SPEED 3
#define TAG_REQUEST_INFO 4
#define TAG_STATISITIC 5
#define TAG_STOP 98

typedef struct
{
    int messageType; // 消息类型
    int junctionId;  // 路口ID
} JunctionMessage;

typedef struct
{
    int messageType; // 消息类型
    int junctionId;  // 路口ID
    int roadId;      // 道路ID
} RoadMessage;

typedef struct
{
    int messageType; // 消息类型
    int passengers;  // 乘客数量
} ControlMessage;

typedef struct
{
    int messageType; // 消息类型
    int junctionId;  // 请求的路口ID
} RequestMessage;

void sendJunctionUpdate(struct VehicleStruct *, int);
void receiveJunctionUpdate(struct JunctionStruct *);
void sendRoadUpdate(struct VehicleStruct *, int);
void receiveRoadUpdate(struct JunctionStruct *);
void sendControlMessage(struct VehicleStruct *, int);
void receiveControlMessage(int *, int *, int *, int *, int *);
void requestRoadSpeeds(struct VehicleStruct *, int, int *);
void handleRoadSpeedRequest(struct JunctionStruct *);
void requestJunctionInfo(struct VehicleStruct *, int, int *);
void handleJunctionInfoRequest(struct JunctionStruct *);
