#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"
#include "pool.h"

// MPI P2P tag to use for command communications, it is important not to reuse this
#define PP_CONTROL_TAG 16384
#define PP_PID_TAG 16383

// Pool options
#define PP_QuitOnNoProcs 1
#define PP_IgnoreOnNoProcs 0
#define PP_DEBUG 0

// Example command package data type which can be extended
static MPI_Datatype PP_COMMAND_TYPE;

// Internal pool global state
static int PP_myRank;
static int PP_numProcs;
static char *PP_active = NULL;
static int PP_processesAwaitingStart;
static struct PP_Control_Package in_command;
static MPI_Request PP_pollRecvCommandRequest = MPI_REQUEST_NULL;

// Internal pool functions
static void errorMessage(char *);
static int startAwaitingProcessesIfNeeded(int, int);
static int handleRecievedCommand();
static void initialiseType();
static struct PP_Control_Package createCommandPackage(enum PP_Control_Command);

/**
 * Initialises the processes pool. Note that a worker will not return from this until it has been instructed to do some work
 * or quit. The return code zero indicates quit, one indicates loop and work for the worker and two indicates that this is the
 * master and it should loop and call master pool.
 */
int processPoolInit()
{
	initialiseType();
	MPI_Comm_rank(MPI_COMM_WORLD, &PP_myRank);
	MPI_Comm_size(MPI_COMM_WORLD, &PP_numProcs);
	if (PP_myRank == 0)
	{
		if (PP_numProcs < 2)
		{
			errorMessage("No worker processes available for pool, run with more than one MPI process");
		}
		PP_active = (char *)malloc(PP_numProcs);
		int i;
		for (i = 0; i < PP_numProcs - 1; i++)
			PP_active[i] = 0;
		PP_processesAwaitingStart = 0;
		if (PP_DEBUG)
			printf("[Master] Initialised Master\n");
		return 2;
	}
	else
	{
		// 工作进程进入阻塞状态等待主进程的命令
		MPI_Recv(&in_command, 1, PP_COMMAND_TYPE, 0, PP_CONTROL_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		return handleRecievedCommand();
	}
}

/**
 * Each process calls this to finalise the process pool
 */
void processPoolFinalise()
{
	if (PP_myRank == 0)
	{
		if (PP_active != NULL)
			free(PP_active);
		int i;
		for (i = 0; i < PP_numProcs - 1; i++)
		{
			if (PP_DEBUG)
				printf("[Master] Shutting down process %d\n", i);
			struct PP_Control_Package out_command = createCommandPackage(PP_STOP);
			MPI_Send(&out_command, 1, PP_COMMAND_TYPE, i + 1, PP_CONTROL_TAG, MPI_COMM_WORLD);
		}
	}
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Type_free(&PP_COMMAND_TYPE);
}

/*
 * Called by the master in a loop, will wait for commands, instruct starts and returns whether to continue polling or
 * zero (false) if the program is to end
 */
int masterPoll()
{
	// 轮询控制中心，接收来自工作进程的命令，并执行相应的动作，仅被主进程调用
	if (PP_myRank == 0)
	{
		MPI_Status status;
		MPI_Recv(&in_command, 1, PP_COMMAND_TYPE, MPI_ANY_SOURCE, PP_CONTROL_TAG, MPI_COMM_WORLD, &status);

		// 收到工作进程的睡眠指令，则将发送命令的工作进程标记为非活跃状态
		if (in_command.command == PP_SLEEPING)
		{
			if (PP_DEBUG)
				printf("[Master] Received sleep command from %d\n", status.MPI_SOURCE);
			PP_active[status.MPI_SOURCE - 1] = 0;
		}

		// 收到任务完成，说明工作进程完成其任务并且希望停止整个程序，主进程返回0表示整个程序可以停止轮询，终止进程池
		if (in_command.command == PP_RUNCOMPLETE)
		{
			if (PP_DEBUG)
				printf("[Master] Received shutdown command\n");
			return 0;
		}

		// 增加等待开始的进程的计数，表示有额外的工作进程请求激活
		if (in_command.command == PP_STARTPROCESS)
		{
			PP_processesAwaitingStart++;
		}
		
		// 不管接收到什么指令，都会试图启动正在等待启动的工作进程
		// 传入正在等待的数量和发出这次指令的进程
		// returnRank的值可能是这次成功启动的mpi的进程排名，或是-1（特殊情况，例如启动失败或不是最后一个）
		int returnRank = startAwaitingProcessesIfNeeded(PP_processesAwaitingStart, status.MPI_SOURCE);

		if (in_command.command == PP_STARTPROCESS)
		{
			// 通常会返回给发起这个指令的进程新启动的进程的rank
			// If the master was to start a worker then send back the process rank that this worker is now on
			MPI_Send(&returnRank, 1, MPI_INT, status.MPI_SOURCE, PP_PID_TAG, MPI_COMM_WORLD);
		}
		return 1;
	}
	else
	{
		errorMessage("Worker process called master poll");
		return 0;
	}
}

/**
 * A worker or the master can instruct to start another worker process
 */
int startWorkerProcess()
{
	if (PP_myRank == 0)
	{
		PP_processesAwaitingStart++;
		return startAwaitingProcessesIfNeeded(PP_processesAwaitingStart, 0);
	}
	else
	{
		// 如果不是被master调用，则向主进程发送开启进程的指令，由主进程启用新的进程
		int workerRank;
		struct PP_Control_Package out_command = createCommandPackage(PP_STARTPROCESS);
		MPI_Send(&out_command, 1, PP_COMMAND_TYPE, 0, PP_CONTROL_TAG, MPI_COMM_WORLD);

		// 通常发送后，主进程会向发起这个指令的进程发回一个启用的进程的rank
		// Receive the rank that this worker has been placed on - if you change the default option from aborting when
		// there are not enough MPI processes then this may be -1
		MPI_Recv(&workerRank, 1, MPI_INT, 0, PP_PID_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		return workerRank;
	}
}

/**
 * A worker can instruct the master to shutdown the process pool. The master can also call this
 * but it does nothing (they just need to call the finalisation step)
 */
void shutdownPool()
{
	// 只能由工作进程调用，会向主线程发送一个请求结束程序和进程池的指令
	if (PP_myRank != 0)
	{
		if (PP_DEBUG)
			printf("[Worker] Commanding a pool shutdown\n");
		struct PP_Control_Package out_command = createCommandPackage(PP_RUNCOMPLETE);
		MPI_Send(&out_command, 1, PP_COMMAND_TYPE, 0, PP_CONTROL_TAG, MPI_COMM_WORLD);
	}
}

/**
 * Called by the worker at the end of each task when it has finished and is to sleep. Will be interrupted from
 * sleeping when given a command from the master, which might be to do more work of shutdown
 * Returns one (true) if the caller should do some more work or zero (false) if it is to quit
 */
int workerSleep()
{
	// 只能被工作进程调用
	if (PP_myRank != 0)
	{	
		// 该指令可能会在主进程执行唤醒工作进程时被发送给工作进程，目前没有别的函数会给子进程发送其他指令
		if (in_command.command == PP_WAKE)
		{
			// 给主进程发送指令说明该工作进程要睡眠，主进程会把该工作进程的active改成0
			// The command was to wake up, it has done the work and now it needs to switch to sleeping mode
			struct PP_Control_Package out_command = createCommandPackage(PP_SLEEPING);
			MPI_Send(&out_command, 1, PP_COMMAND_TYPE, 0, PP_CONTROL_TAG, MPI_COMM_WORLD);

			// 如果有一个非阻塞操作被启动了但是未完成，则等待直到该操作完成
			if (PP_pollRecvCommandRequest != MPI_REQUEST_NULL)
				MPI_Wait(&PP_pollRecvCommandRequest, MPI_STATUS_IGNORE);
		}
		return handleRecievedCommand();
	}
	else
	{
		errorMessage("Master process called worker poll");
		return 0;
	}
}

/**
 * Retrieves the data associated with the latest command, this is just an illustration of how you can associate optional
 * data with pool commands and we commonly associate the parent ID of a started worker
 */
int getCommandData()
{
	return in_command.data;
}

/**
 * Determines whether or not the worker should stop (i.e. the master has send the STOP command to all workers)
 */
int shouldWorkerStop()
{
	// 先检查非阻塞操作是否全部完成
	if (PP_pollRecvCommandRequest != MPI_REQUEST_NULL)
	{
		int flag;
		MPI_Test(&PP_pollRecvCommandRequest, &flag, MPI_STATUS_IGNORE);

		// 该工作进程是否收到停止指令，例如调用processPoolFinalise发出的PP_STOP，则返回1，可以结合break使工作进程跳出循环
		if (flag && in_command.command == PP_STOP)
		{
			// If there's a message waiting, and it's a stop call then return 1 to denote stop
			return 1;
		}
	}
	return 0;
}

/**
 * Called by the master and will start awaiting processes (signal to them to start) if required.
 * By default this works as a process pool, so if there are not enough processes to workers then it will quit
 * but this can be changed by modifying options in the pool.
 * It takes in the awaiting worker number to send rank data to/from, the parent rank of the process to start
 * and returns the process rank that this awaiting worker was on. In the default case of #workers > pool capacity
 * causing an abort, then this will only be called to start single workers and as such the parent ID and return rank
 * will match perfectly. If you change the options to allow for workers to queue up if there is not enough MPI
 * capacity then the parent and return rank will be -1 for all workers started which do not match the provided awaiting Id
 */
static int startAwaitingProcessesIfNeeded(int awaitingId, int parent)
{
	int awaitingProcessMPIRank = -1;

	// 如果存在正在等待被启动的进程
	if (PP_processesAwaitingStart)
	{
		int i;
		for (i = 0; i < PP_numProcs - 1; i++)
		{
			// 遍历所有可用的进程，找到第一个非活跃状态的进程
			if (!PP_active[i])
			{
				PP_active[i] = 1;
				struct PP_Control_Package out_command = createCommandPackage(PP_WAKE);

				// 如果awaitingID等于PP_processesAwaitingStart，则说明当前正在处理启动的进程是最后一个请求启动的进程
				// 如果.data等于-1，说明这个启动请求无法直接关联到特定的父进程
				out_command.data = awaitingId == PP_processesAwaitingStart ? parent : -1;
				if (PP_DEBUG)
					printf("[Master] Starting process %d\n", i + 1);
				MPI_Send(&out_command, 1, PP_COMMAND_TYPE, i + 1, PP_CONTROL_TAG, MPI_COMM_WORLD);

				// 如果是最后一个，则记录目前正在处理启动的mpi的排名，返回这个排名
				if (awaitingId == PP_processesAwaitingStart)
					awaitingProcessMPIRank = i + 1; // Will return this rank to the caller

				// 减少计数，若计数为0则退出循环
				PP_processesAwaitingStart--;
				if (PP_processesAwaitingStart == 0)
					break;
			}
			if (i == PP_numProcs - 2)
			{
				// If I reach this point, I must have looped through the whole array and found no available processes
				if (PP_QuitOnNoProcs)
				{
					errorMessage("No more processes available");
				}

				if (PP_IgnoreOnNoProcs)
				{
					fprintf(stderr, "[ProcessPool] Warning. No processes available. Ignoring launch request.\n");
					PP_processesAwaitingStart--;
				}
				// otherwise, do nothing; a process may become available on the next iteration of the loop
			}
		}
	}
	return awaitingProcessMPIRank;
}

/**
 * Called by the worker once we have received a pool command and will determine what to do next
 */
static int handleRecievedCommand()
{
	// 用于工作进程处理从主进程接收到的命令，可能会被唤醒或被停止
	// We have just (most likely) received a command, therefore decide what to do
	if (in_command.command == PP_WAKE)
	{
		// 接收到唤醒指令，继续等待接收下一个工作指令，返回1表示已经唤醒
		// If we are told to wake then post a recv for the next command and return true to continues
		MPI_Irecv(&in_command, 1, PP_COMMAND_TYPE, 0, PP_CONTROL_TAG, MPI_COMM_WORLD, &PP_pollRecvCommandRequest);
		if (PP_DEBUG)
			printf("[Worker] Process %d woken to work\n", PP_myRank);
		return 1;
	}
	else if (in_command.command == PP_STOP)
	{
		// 接到停止指令
		// Stopping so return zero to denote stop
		if (PP_DEBUG)
			printf("[Worker] Process %d commanded to stop\n", PP_myRank);
		return 0;
	}
	else
	{
		// 其他错误指令
		errorMessage("Unexpected control command");
		return 0;
	}
}

/**
 * Writes an error message to stderr and MPI Aborts
 */
static void errorMessage(char *message)
{
	fprintf(stderr, "%4d: [ProcessPool] %s\n", PP_myRank, message);
	MPI_Abort(MPI_COMM_WORLD, 1);
}

/**
 * Initialises the command package MPI type, we use this to illustrate how additional information (this case
 * the parent rank) can be associated with commands
 */
static void initialiseType()
{
	struct PP_Control_Package package;
	MPI_Aint pckAddress, dataAddress;
	MPI_Address(&package, &pckAddress);
	MPI_Address(&package.data, &dataAddress);
	int blocklengths[3] = {1, 1}, nitems = 2;
	MPI_Datatype types[3] = {MPI_CHAR, MPI_INT};
	MPI_Aint offsets[3] = {0, dataAddress - pckAddress};
	MPI_Type_create_struct(nitems, blocklengths, offsets, types, &PP_COMMAND_TYPE);
	MPI_Type_commit(&PP_COMMAND_TYPE);
}

/**
 * A helper function which will create a command package from the desired command
 */
static struct PP_Control_Package createCommandPackage(enum PP_Control_Command desiredCommand)
{
	struct PP_Control_Package package;
	package.command = desiredCommand;
	return package;
}
