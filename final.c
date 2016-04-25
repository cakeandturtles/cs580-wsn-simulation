#include "simlib.h"
#include <math.h>
#include <stdio.h>

#define FIRE_START 1
#define FIRE_END 2
#define NODE_FAULT 3
#define NODE_REPAIR 4
#define MEASURE_NODES 5
//simulate for 1 hour (in seconds)
#define MAX_TIME 216000
#define FAULT_TEMPERATURE 579;
//in meters
#define AREA_WIDTH 6.0
#define AREA_HEIGHT 4.0

typedef struct {
    float x;
    float y;
    float temperature;
    float *readings;
} Node;

int   num_clusters, num_nodes, total_fires_started, num_faulty_nodes,
      total_nodes_faulted, m;
float mean_interfire, mean_interfirestop, mean_internodefault,
      mean_internoderepair, mean_intermeasure;
float *cluster_bounds_x1, *cluster_bounds_y1, *cluster_bounds_x2, *cluster_bounds_y2;
Node **nodes;

void initialize(void);
void initializeClusters(void);
void fireStart(void);
float dist(float, float, float, float);
void fireEnd(void);
void nodeFault(void);
void nodeRepair(void);
void measureNodes(void);
void report(void);

int main()
{
    /* initializes simlib */
    init_simlib();

    /* read in input parameters wink wink */
    m = 3;
    num_clusters = 3;
    num_nodes = 8;
    mean_interfire = 180.0; //every 3 minutes
    mean_interfirestop = 60.0; //lasts for a minute
    mean_internodefault = 300.0; //every five minute
    mean_internoderepair = 120.0; //takes two minutes
    mean_intermeasure = 60.0; //every minute

    /* Initialize the simulation */
    initialize();

    while (sim_time < MAX_TIME){
        /* determine the next event. */
        timing();

        /* update time-average statistical accumulators. ??? */

        /* Invoke the appropriate event function */
        //printf("%d\n", next_event_type);
        switch (next_event_type){
            case FIRE_START:
                fireStart();
                break;
            case FIRE_END:
                fireEnd();
                break;
            case NODE_FAULT:
                nodeFault();
                break;
            case NODE_REPAIR:
                nodeRepair();
                break;
            case MEASURE_NODES:
                measureNodes();
                break;
        }
    }

    /* Invoke the report generator and end the simulation */
    report();

    return 0;
}

void report()
{
    printf("total fires: %d\n", total_fires_started);
    printf("current faulty nodes: %d\n", num_faulty_nodes);
    printf("total faulty nodes: %d\n", total_nodes_faulted);
}

void initialize()
{
    //seed the random number generator
    clock_t clk;
    clk = clock();
    lcgrandst(clk, 1);

    //initialize measurement variables
    total_fires_started = 0;
    num_faulty_nodes = 0;
    total_nodes_faulted = 0;

    //initialize the clusters
    initializeClusters();

    //schedule next temperature change
    event_schedule(sim_time+expon(mean_interfire, 1), FIRE_START);
    //because no fires at start, can't stop any!
    event_schedule(sim_time+INFINITY, FIRE_END);
    //schedule next node fault
    event_schedule(sim_time+expon(mean_internodefault, 1), NODE_FAULT);
    //because no nodes start at fault, we can't repair any!
    event_schedule(INFINITY, NODE_REPAIR);
    //schedule next node measurement
    event_schedule(sim_time+expon(mean_intermeasure, 1), MEASURE_NODES);
}

void initializeClusters()
{
    //make space for cluster bounds and nodes
    cluster_bounds_x1 = (float *)malloc(sizeof(float)*num_clusters);
    cluster_bounds_y1 = (float *)malloc(sizeof(float)*num_clusters);
    cluster_bounds_x2 = (float *)malloc(sizeof(float)*num_clusters);
    cluster_bounds_y2 = (float *)malloc(sizeof(float)*num_clusters);
    nodes = (Node **)malloc(sizeof(Node *)*num_clusters);

    int i;

    //actually set cluster bounds
    if (num_clusters == 1){
        cluster_bounds_x1[0] = 0;
        cluster_bounds_y1[0] = 0;
        cluster_bounds_x2[0] = AREA_WIDTH;
        cluster_bounds_y2[0] = AREA_HEIGHT;
    }
    else if (num_clusters % 2 == 1){ //odd number
        int num_top = 1 + ((num_clusters - 1) / 2);
        float w = AREA_WIDTH / num_top;
        float h = AREA_HEIGHT / 2.0;
        float x = 0;
        for (i = 0; i < num_top; i++){
            cluster_bounds_y1[i] = 0;
            cluster_bounds_y2[i] = h;
            cluster_bounds_x1[i] = x;
            x += w;
            cluster_bounds_x2[i] = x;
        }
        x = (w / 2.0);
        for (i = num_top; i < num_clusters; i++){
            cluster_bounds_y1[i] = h;
            cluster_bounds_y2[i] = AREA_HEIGHT;
            cluster_bounds_x1[i] = x;
            x += w;
            cluster_bounds_x2[i] = x;
        }
    }
    else{ //even number
        float w = AREA_WIDTH / (num_clusters / 2);
        float h = AREA_HEIGHT / 2;
        float x = 0;
        for (i = 0; i < num_clusters; i+=2){
            cluster_bounds_y1[i] = 0;
            cluster_bounds_y2[i] = h;
            cluster_bounds_y1[i+1] = h;
            cluster_bounds_y2[i+1] = AREA_HEIGHT;

            cluster_bounds_x1[i] = x;
            cluster_bounds_x1[i+1] = x;
            x += w;
            cluster_bounds_x2[i] = x;
            cluster_bounds_x2[i+1] = x;
        }
    }

    //now, popular clusters with nodes
    int j;
    for (i = 0; i < num_clusters; i++){
        nodes[i] = (Node *)malloc(sizeof(Node)*num_nodes);
        float x1 = cluster_bounds_x1[i];
        float x2 = cluster_bounds_x2[i];
        float y1 = cluster_bounds_y1[i];
        float y2 = cluster_bounds_y2[i];

        float xt;
        float yt;
        int half = 1 + ((num_nodes - 1) / 2);
        float w = (x2 - x1) / half;
        float h = (y2 - y1) / 2;

        //odd number of nodes
        if (num_nodes % 2 == 1){
            xt = w/2;
            yt = 0;
            for (j = 0; j < half; j++){
                Node *node = (Node *)malloc(sizeof(Node));
                node->x = xt + w/2;
                node->y = yt + h/2;
                node->temperature = 21.0; //room temperature, initially
                node->readings = (float *)malloc(sizeof(float)*m);
                int k;
                for (k = 0; k < m; k++){
                    node->readings[k] = node->temperature;
                }
                nodes[i][j] = *node;
                xt += w;
            }
            xt = 0;
            yt = h;
            for (j = half; j < num_nodes; j++){
                Node *node = (Node *)malloc(sizeof(Node));
                node->x = xt + w/2;
                node->y = yt + h/2;
                node->temperature = 21.0; //room temperature, initially
                nodes[i][j] = *node;
                xt += w;
            }
        }
        //even number of nodes
        else{
            xt = 0;
            yt = 0;
            for (j = 0; j < num_nodes; j+=2){
                Node *node = (Node *)malloc(sizeof(Node));
                node->x = xt + w/2;
                node->y = 0 + h/2;
                node->temperature = 21.0; //room temperature, initially
                nodes[i][j] = *node;

                node = (Node *)malloc(sizeof(Node));
                node->x = xt + w/2;
                node->y = h + h/2;
                node->temperature = 21.0; //room temperature, initially
                nodes[i][j+1] = *node;

                xt += w;
            }
        }
    }
}

float dist(float x1, float y1, float x2, float y2)
{
    return sqrt(pow(x2-x1, 2) + pow(y2-y1, 2));
}

void fireStart()
{
    /* a fire will pick a random location on the space,
        affect nodes by how close it is to it */

    //update metrics
    total_fires_started++;

    //randomly set fire attributes
    float fire_x = uniform(0, AREA_WIDTH, 1);
    float fire_y = uniform(0, AREA_HEIGHT, 1);
    float fire_radius = uniform(0.5, 2.0, 1);

    //now, affect all the nodes affected by the fire
    int i, j;
    for (i = 0; i < num_clusters; i++){
        for (j = 0; j < num_nodes; j++){
            Node node = nodes[i][j];
            float distance = dist(node.x, node.y, fire_x, fire_y);

            //if node is unaffected by the fire, continue
            if (distance > fire_radius) continue;

            //otherwise, change the temperature by the fire amount!!!
            //difference from room temperature to fire (in celcius)
            /* NOTE: if multiple fires affect the same node, the temperature
                    can get really big, but as long as we only measure over
                    a certain threshold when we measure node temperature, it
                    should be OK*/
            node.temperature += FAULT_TEMPERATURE;
        }
    }

    //now, we need to schedule the fire to end!!!!
    transfer[3] = fire_x;
    transfer[4] = fire_y;
    transfer[5] = fire_radius;
    event_schedule(sim_time+expon(mean_interfirestop, 1), FIRE_END);

    //also, schedule the next fire start!
    event_schedule(sim_time+expon(mean_interfire, 1), FIRE_START);
}

void fireEnd()
{
    float fire_x = transfer[3];
    float fire_y = transfer[4];
    float fire_radius = transfer[5];

    //since the fire is ending, de-affect all affected nodes
    int i, j;
    for (i = 0; i < num_clusters; i++){
        for (j = 0; j < num_nodes; j++){
            Node node = nodes[i][j];
            float distance = dist(node.x, node.y, fire_x, fire_y);

            //the node was never affected by this fire
            if (distance > fire_radius) continue;

            node.temperature -= FAULT_TEMPERATURE;
        }
    }
}

void nodeFault()
{
    //update metrics
    num_faulty_nodes++;
    total_nodes_faulted++;

    //pick a random cluster and node to fault from!!
    int cluster_index = (int)floor(uniform(0, num_clusters, 1));
    int node_index = (int)floor(uniform(0, num_nodes, 1));

    //fault the node!!!
    Node node = nodes[cluster_index][node_index];
    node.temperature += FAULT_TEMPERATURE;

    //schedule a repair???
    transfer[3] = cluster_index;
    transfer[4] = node_index;
    event_schedule(sim_time+expon(mean_internoderepair, 1), NODE_REPAIR);

    //also, schedule the next node fault!
    event_schedule(sim_time+expon(mean_internodefault, 1), NODE_FAULT);
}

void nodeRepair()
{
    //update (metrics)?
    num_faulty_nodes--;

    //pick the cluster/node indices from the event list
    int cluster_index = transfer[3];
    int node_index = transfer[4];

    //fix the node!!!
    Node node = nodes[cluster_index][node_index];
    node.temperature -= FAULT_TEMPERATURE;
}

void measureNodes()
{
}
