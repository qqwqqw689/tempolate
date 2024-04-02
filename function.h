time_t getCurrentSeconds();
int getRandomInteger(int, int);
int planRoute(int, int, int, struct JunctionStruct *);
void loadRoadMap(char *, struct JunctionStruct **, int *, int *);
int findAppropriateRoad(int, struct JunctionStruct *);
int findIndexOfMinimum(double *, char *, int);