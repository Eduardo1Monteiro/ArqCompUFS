#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef struct {
    int position;
    int priority;
    int size;
    char data[512][3];
} Packet;

typedef struct {
    FILE *input;
    FILE *output;
} Files;

Files openFiles(char *argv[]) {
    Files files;
    files.input = fopen(argv[1], "r");
    if (files.input == NULL) {
        fprintf(stderr, "Error: Could not open input file %s\n", argv[1]);
        exit(1);
    }

    files.output = fopen(argv[2], "w");
    if (files.output == NULL) {
        fprintf(stderr, "Error: Could not open output file %s\n", argv[2]);
        fclose(files.input);
        exit(1);
    }
    return files;
}

void swap(Packet *a, Packet *b) {
    Packet temp = *a;
    *a = *b;
    *b = temp;
}

void heapify(Packet packets[], int currentHeapSize, int rootIndex) {
    int smallestIndex = rootIndex;
    int leftChildIndex = 2 * rootIndex + 1;
    int rightChildIndex = 2 * rootIndex + 2;

    if (leftChildIndex < currentHeapSize && 
        packets[leftChildIndex].priority < packets[smallestIndex].priority) {
        smallestIndex = leftChildIndex;
    }

    if (rightChildIndex < currentHeapSize && 
        packets[rightChildIndex].priority < packets[smallestIndex].priority) {
        smallestIndex = rightChildIndex;
    }

    if (smallestIndex != rootIndex) {
        swap(&packets[rootIndex], &packets[smallestIndex]);
        heapify(packets, currentHeapSize, smallestIndex);
    }
}

void heapSort(Packet packets[], int totalPackets) {
    for (int i = totalPackets / 2 - 1; i >= 0; i--) {
        heapify(packets, totalPackets, i);
    }

    for (int i = totalPackets - 1; i > 0; i--) {
        swap(&packets[0], &packets[i]);
        heapify(packets, i, 0);
    }
}

void printBatch(FILE *out, Packet buffer[], int count, int batchId) {
    heapSort(buffer, count);

    for (int i = 0; i < count; i++) {
        fprintf(out, "|");
        for (int j = 0; j < buffer[i].size; j++) {
            fprintf(out, "%s", buffer[i].data[j]);
            if (j < buffer[i].size - 1) {
                fprintf(out, ",");
            }
        }
    }
    fprintf(out, "|\n");
}

int main(int argc, char *argv[]) {

    Files files = openFiles(argv);

    int nPackets, maxCapacity;
    if (fscanf(files.input, "%d %d", &nPackets, &maxCapacity) != 2) {
        return 1; 
    }

    Packet *buffer = (Packet *)malloc(sizeof(Packet) * nPackets);
    int bufferCount = 0;
    int currentBytes = 0;
    int batchId = 1;

    for (int i = 0; i < nPackets; i++) {
        Packet p;
        p.position = i;
        
        fscanf(files.input, "%d %d", &p.priority, &p.size);

        for (int j = 0; j < p.size; j++) {
            fscanf(files.input, "%s", p.data[j]);
        }

        if (currentBytes + p.size > maxCapacity) {
            printBatch(files.output, buffer, bufferCount, batchId);
            
            bufferCount = 0;
            currentBytes = 0;
            batchId++;
        }

        buffer[bufferCount] = p;
        bufferCount++;
        currentBytes += p.size;
    }

    if (bufferCount > 0) {
        printBatch(files.output, buffer, bufferCount, batchId);
    }

    free(buffer);
    fclose(files.input);
    fclose(files.output);

    return 0;
}
