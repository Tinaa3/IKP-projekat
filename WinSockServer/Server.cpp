#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../Common/QueueHeader.h"

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "5059"
#define SAFE_DELETE_HANDLE(a) if(a){CloseHandle(a);} 
#define THREAD_POOL_SIZE 10

HANDLE hAddToQueueSemaphore;
HANDLE hLoadBalancerThread1Semaphore;
HANDLE hThreadPoolSemaphore[THREAD_POOL_SIZE];
HANDLE hThreadPoolSemaphoreFinish[THREAD_POOL_SIZE];
HANDLE hThreadPoolThread[THREAD_POOL_SIZE];

CRITICAL_SECTION QueueCS;

struct Queue* queue;

bool busyThreads[THREAD_POOL_SIZE];
int counter = 0;

int numOfWorkerRoles = -1;

bool InitializeWindowsSockets();

void WriteInFile(int data, int workerRole);

DWORD WINAPI addToQueue(LPVOID lpParam);

DWORD WINAPI loadBalancerThread1(LPVOID lpParam);

DWORD WINAPI loadBalancerThread2(LPVOID lpParam);

DWORD WINAPI workerRole(LPVOID lpParam);

int main(void)
{
	int iResult;
	int currentConnections = 0;
	char recvbuf[DEFAULT_BUFLEN];

	char recvData[10];
	
	queue = createQueue(10);

	SOCKET listenSocket = INVALID_SOCKET;

	DWORD addToQueueThreadID;
	DWORD loadBalancerThread1ID;
	DWORD loadBalancerThread2ID;

	HANDLE hAddToQueueThread;
	HANDLE hLoadBalancerThread1;
	HANDLE hLoadBalancerThread2;

	hAddToQueueSemaphore = CreateSemaphore(0, 0, THREAD_POOL_SIZE, NULL);
	if (hAddToQueueSemaphore) {
		hAddToQueueThread = CreateThread(NULL, 0, &addToQueue, recvData, 0, &addToQueueThreadID);
	}

	hLoadBalancerThread1Semaphore = CreateSemaphore(0, 0, THREAD_POOL_SIZE, NULL);
	if (hLoadBalancerThread1Semaphore) {
		hLoadBalancerThread1 = CreateThread(NULL, 0, &loadBalancerThread1, NULL, 0, &loadBalancerThread1ID);
	}

	hLoadBalancerThread2 = CreateThread(NULL, 0, &loadBalancerThread2, NULL, 0, &loadBalancerThread2ID);

	
	InitializeCriticalSection(&QueueCS);

	SOCKET acceptedSocket;	
	acceptedSocket = INVALID_SOCKET;

	if (InitializeWindowsSockets() == false)
		return 1;

	addrinfo *resultingAddress = NULL;
	addrinfo hints;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &resultingAddress);
	if (iResult != 0) {
		printf("getaddrinfo failed with error: %d\n", iResult);
		WSACleanup();
		return 1;
	}

	listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listenSocket == INVALID_SOCKET) {
		printf("socket failed with error: %ld\n", WSAGetLastError());
		freeaddrinfo(resultingAddress);
		WSACleanup();
		return 1;
	}

	iResult = bind(listenSocket, resultingAddress->ai_addr, (int)resultingAddress->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("bind failed with error: %d\n", WSAGetLastError());
		freeaddrinfo(resultingAddress);
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	freeaddrinfo(resultingAddress);

	iResult = listen(listenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR) {
		printf("listen failed with error: %d\n", WSAGetLastError());
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	unsigned long mode = 1;
	iResult = ioctlsocket(listenSocket, FIONBIO, &mode);

	printf("Server initialized, waiting for clients.\n");

	fd_set readfds;

	timeval timeVal;
	timeVal.tv_sec = 1;
	timeVal.tv_usec = 0;

	while (true) {
		FD_ZERO(&readfds);

		if (currentConnections == 0)
			FD_SET(listenSocket, &readfds);
		else
			FD_SET(acceptedSocket, &readfds);
		
		int result = select(0, &readfds, NULL, NULL, &timeVal);
		if (result == 0)
			continue;
		else if (result == SOCKET_ERROR)
			break;
		else
		{
			if (FD_ISSET(listenSocket, &readfds)) {
				acceptedSocket = accept(listenSocket, NULL, NULL);
				if (acceptedSocket == INVALID_SOCKET) {
					printf("accept failed with error: %d\n", WSAGetLastError());
					closesocket(listenSocket);
					WSACleanup();
					return 1;
				}

				unsigned long mode = 1;
				iResult = ioctlsocket(acceptedSocket, FIONBIO, &mode);
				currentConnections++;
			}
		
			if (FD_ISSET(acceptedSocket, &readfds)) {
				if (isFull(queue)) {
					printf("Queue is full!. Your data will be received when free slots open!\n");
					Sleep(2000);
					
				}
				
				iResult = recv(acceptedSocket, recvbuf, DEFAULT_BUFLEN, 0);
				
				if (iResult > 0) {  
						memcpy(recvData, recvbuf, sizeof(recvbuf));
						printf("Client sent: %s\n", recvData);
							
						ReleaseSemaphore(hAddToQueueSemaphore, 1, NULL);
				}
				else if (iResult == 0) {
					printf("Connection with client closed.\n");
					closesocket(acceptedSocket);
					currentConnections--;
				}
				else {
					printf("recv failed with error: %d\n", WSAGetLastError());
					closesocket(acceptedSocket);
					currentConnections--;
				}
			}		
		}
	}

	if (hAddToQueueThread)
		WaitForSingleObject(hAddToQueueThread, INFINITE);
	if (hLoadBalancerThread1)
		WaitForSingleObject(hLoadBalancerThread1, INFINITE);
	if (hLoadBalancerThread2)
		WaitForSingleObject(hLoadBalancerThread2, INFINITE);

	iResult = shutdown(acceptedSocket, SD_SEND);
	if (iResult == SOCKET_ERROR) {
		printf("shutdown failed with error: %d\n", WSAGetLastError());
		closesocket(acceptedSocket);
		WSACleanup();
		return 1;
	}

	closesocket(acceptedSocket);

	SAFE_DELETE_HANDLE(hAddToQueueSemaphore);
	SAFE_DELETE_HANDLE(hAddToQueueThread);
	SAFE_DELETE_HANDLE(hLoadBalancerThread1Semaphore);
	SAFE_DELETE_HANDLE(hLoadBalancerThread1);
	SAFE_DELETE_HANDLE(hLoadBalancerThread2);
		
	closesocket(listenSocket);
	DeleteCriticalSection(&QueueCS);
	WSACleanup();

	return 0;
}

bool InitializeWindowsSockets() {
	WSADATA wsaData;

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
		printf("WSAStartup failed with error: %d\n", WSAGetLastError());
		return false;
	}
	return true;
}

void WriteInFile(int data, int workerRole) {
	const char* filename = "output.txt";

	FILE *fp = fopen(filename, "a");
	if (fp == NULL) {
		printf("Error opening the file %s", filename);
		return;
	}

	fprintf(fp, "Worker role %d: %d\n", workerRole, data);
	fclose(fp);
}

DWORD WINAPI addToQueue(LPVOID lpParam) {
	while (true) {
		WaitForSingleObject(hAddToQueueSemaphore, INFINITE);
		
		EnterCriticalSection(&QueueCS);
			char* temp = (char*)lpParam;
			
			if (!enqueue(queue, atoi(temp))) {
				printf("Q is full!");
				return -1;
				
			}
			
			printf("Enqueued data: %s\n", temp);
		LeaveCriticalSection(&QueueCS);
		
		ReleaseSemaphore(hLoadBalancerThread1Semaphore, 1, NULL);
	}

	return 0;
}

DWORD WINAPI loadBalancerThread1(LPVOID lpParam) {
	while (true) {
		WaitForSingleObject(hLoadBalancerThread1Semaphore, INFINITE);
		
		bool found = false;
		while(!found) {
			if (!busyThreads[counter]) {
				ReleaseSemaphore(hThreadPoolSemaphore[counter], 1, NULL);
				busyThreads[counter] = true;
				printf("Sent to workerRole %d.\n", counter);
				found = true;
			}

			counter++;
			counter = counter % numOfWorkerRoles;
		} 		
	}
	return 0;
}

DWORD WINAPI loadBalancerThread2(LPVOID lpParam) {
	DWORD threadPoolThreadID[THREAD_POOL_SIZE];
	numOfWorkerRoles = 1;

	for (int i = 0; i < THREAD_POOL_SIZE; i++) {
		busyThreads[i] = false;
	}

	for (int i = 0; i < numOfWorkerRoles; i++) {
		hThreadPoolSemaphore[i] = CreateSemaphore(0, 0, 1, NULL);
		hThreadPoolSemaphoreFinish[i] = CreateSemaphore(0, 0, 1, NULL);
		if (hThreadPoolSemaphore[i] && hThreadPoolSemaphoreFinish[i]) {
			hThreadPoolThread[i] = CreateThread(NULL, 0, &workerRole, (LPVOID)i, 0, &threadPoolThreadID[i]);
		}	
	}
	
	while (true)
	{
		double result = ((double)queue->size / (double)queue->capacity) * 100;
		printf("Current size of Q is: %.2f %%\n", result);

		if (result > 70) {
			hThreadPoolSemaphore[numOfWorkerRoles] = CreateSemaphore(0, 0, 1, NULL);
			hThreadPoolSemaphoreFinish[numOfWorkerRoles] = CreateSemaphore(0, 0, 1, NULL);

			if (hThreadPoolSemaphore[numOfWorkerRoles] && hThreadPoolSemaphoreFinish[numOfWorkerRoles]) {
				hThreadPoolThread[numOfWorkerRoles] = CreateThread(NULL, 0, &workerRole, (LPVOID)numOfWorkerRoles, 0, &threadPoolThreadID[numOfWorkerRoles]);
				numOfWorkerRoles++;
			}
		}
		else if (result < 30 && numOfWorkerRoles > 1) {
			ReleaseSemaphore(hThreadPoolSemaphoreFinish[numOfWorkerRoles - 1], 1, NULL);
			numOfWorkerRoles--;
		}
		
		Sleep(2000);
	}

	return 0;
}

DWORD WINAPI workerRole(LPVOID lpParam) {
	int n = (int)lpParam;
	const int semaphore_num = 2;
	HANDLE semaphores[semaphore_num] = { hThreadPoolSemaphoreFinish[n],hThreadPoolSemaphore[n] };
		
	while (WaitForMultipleObjects(semaphore_num, semaphores, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
		EnterCriticalSection(&QueueCS);
			
			int workerRoleData = dequeue(queue);
		LeaveCriticalSection(&QueueCS);

		if (workerRoleData == -1)
			break;

		Sleep(7000);
		WriteInFile(workerRoleData, n);
		printf("Hello from workerRole %d. Dequeued data: %d\n", n, workerRoleData);
		
		busyThreads[n] = false;
	}

	SAFE_DELETE_HANDLE(hThreadPoolSemaphore[n]);
	SAFE_DELETE_HANDLE(hThreadPoolSemaphoreFinish[n]);
	SAFE_DELETE_HANDLE(hThreadPoolThread[n]);
	return 0;
} 