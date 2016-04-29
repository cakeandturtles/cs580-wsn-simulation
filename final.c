#include "simlib.h"
#include <math.h>
#include <stdio.h>
#include <stdbool.h>

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
    bool is_faulty;
    bool is_outlier;
    float *readings;
    int support;
    bool duplicate;
    float *LSH;
} Node;

int   num_clusters, num_nodes, total_fires_started, m, b,
      num_measurements, min_sup_local, min_sup_group, similarity_threshold;
float mean_interfire, mean_interfirestop, mean_internodefault,
      mean_internoderepair, mean_intermeasure;
float *cluster_bounds_x1, *cluster_bounds_y1, *cluster_bounds_x2, *cluster_bounds_y2;
Node ***nodes;

void initialize(void);
void initializeClusters(void);
Node *initNode(float, float);
void fireStart(void);
float dist(float, float, float, float);
void fireEnd(void);
void nodeFault(void);
void nodeRepair(void);
void measureNodes(void);
void detectOutliers(void);
void report(void);

int main()
{
    /* initializes simlib */
    init_simlib();

    /* read in input parameters wink wink */
    m = 5;
    b = 3;
    num_clusters = 3;
    num_nodes = 8;
    mean_interfire = 300.0; //every five minutes
    mean_interfirestop = 300.0; //lasts for five minutes
    mean_internodefault = 600.0; //every 10 minute
    mean_internoderepair = 6000.0; //takes 100 minutes
    mean_intermeasure = 180.0; //every three minutes
    min_sup_local = 3;
    min_sup_group = 4;
    similarity_threshold = 1;

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
                //printf("fire start\n");
                fireStart();
                break;
            case FIRE_END:
                //printf("fire end\n");
                fireEnd();
                break;
            case NODE_FAULT:
                //printf("node fault\n");
                nodeFault();
                break;
            case NODE_REPAIR:
                //printf("node repair\n");
                nodeRepair();
                break;
            case MEASURE_NODES:
                //printf("measure_nodes\n");
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
    printf("TODO: stuff from detectOutliers function\n");
}

void initialize()
{
    //seed the random number generator
    clock_t clk;
    clk = clock();
    printf("clock: %f\n", (float)clk);
    lcgrandst(clk, 1);

    //initialize the clusters
    initializeClusters();

    //initialize other state variables
    num_measurements = 0;

    //initialize measurement variables
    total_fires_started = 0;

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
    nodes = (Node ***)malloc(sizeof(Node **)*num_clusters);

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
        nodes[i] = (Node **)malloc(sizeof(Node *)*num_nodes);
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
            for (j = 0; j < half; j++){
                Node *node = initNode(xt + w/2, 0 + h/2);
                nodes[i][j] = node;
                xt += w;
            }
            xt = 0;
            for (j = half; j < num_nodes; j++){
                Node *node = initNode(xt + w/2, h + h/2);
                nodes[i][j] = node;
                xt += w;
            }
        }
        //even number of nodes
        else{
            xt = 0;
            yt = 0;
            for (j = 0; j < num_nodes; j+=2){
                Node *node = initNode(xt + w/2, 0 + h/2);
                nodes[i][j] = node;

                node = initNode(xt + w/2, h + h/2);
                nodes[i][j+1] = node;

                xt += w;
            }
        }
    }
}

Node *initNode(float x, float y){
    int i;
    Node *node = (Node *)malloc(sizeof(Node));
    node->x = x;
    node->y = y;
    node->temperature = 21.0; //room temperature, initially
    node->is_faulty = false;
    node->support = 0;
    node->readings = (float *)malloc(sizeof(float)*m);
    for (i = 0; i < m; i++){
        node->readings[i] = node->temperature;
    }
    node->LSH = (float *)malloc(sizeof(float)*b);
    for (i = 0; i < b; i++){
        node->LSH[i] = 0.0;
    }
    return node;
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
            Node *node = nodes[i][j];
            float distance = dist(node->x, node->y, fire_x, fire_y);

            //if node is unaffected by the fire, continue
            if (distance > fire_radius) continue;

            //otherwise, change the temperature by the fire amount!!!
            //difference from room temperature to fire (in celcius)
            /* NOTE: if multiple fires affect the same node, the temperature
                    can get really big, but as long as we only measure over
                    a certain threshold when we measure node temperature, it
                    should be OK*/
            node->temperature += FAULT_TEMPERATURE;

            //printf("Fire affect node: i: %d, j: %d, TEMP: %f\n", i, j, node->temperature);
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
            Node *node = nodes[i][j];
            float distance = dist(node->x, node->y, fire_x, fire_y);

            //the node was never affected by this fire
            if (distance > fire_radius) continue;

            node->temperature -= FAULT_TEMPERATURE;
        }
    }
}

void nodeFault()
{
    //pick a random cluster and node to fault from!!
    int cluster_index = (int)floor(uniform(0, num_clusters, 1));
    int node_index = (int)floor(uniform(0, num_nodes, 1));

    //fault the node!!!
    Node *node = nodes[cluster_index][node_index];
    node->temperature += FAULT_TEMPERATURE;
    node->is_faulty = true;

    //schedule a repair???
    transfer[3] = cluster_index;
    transfer[4] = node_index;
    event_schedule(sim_time+expon(mean_internoderepair, 1), NODE_REPAIR);

    //also, schedule the next node fault!
    event_schedule(sim_time+expon(mean_internodefault, 1), NODE_FAULT);
}

void nodeRepair()
{
    //pick the cluster/node indices from the event list
    int cluster_index = transfer[3];
    int node_index = transfer[4];

    //fix the node!!!
    Node *node = nodes[cluster_index][node_index];
    node->temperature -= FAULT_TEMPERATURE;
    node->is_faulty = false;
}

void measureNodes()
{
    //make all the nodes take a new measurement!!
    int i, j, k;
    for (i = 0; i < num_clusters; i++){
        for (j = 0; j < num_nodes; j++){
            Node *node = nodes[i][j];

            //move all the readings over one, discarding the last
            for (k = m-2; k >= 0; k--){
                node->readings[k+1] = node->readings[k];
            }
            //do a new reading!!!
            node->readings[0] = node->temperature;

            //reset FTDA variables
            node->support = 0;
        }
    }

    num_measurements++;
    //if we've conducted m measurements, can perform an FTDA!
    if (num_measurements == m){
        num_measurements = 0;

        detectOutliers();
    }

    //also, schedule the next node measurement!
    event_schedule(sim_time+expon(mean_intermeasure, 1), MEASURE_NODES);
}

bool isSimilar(float *LSH1, float *LSH2)
{
    //cheat!!! check if the number of similar codes is greater than a similarity threshhold int
    int num_similar, i;
    num_similar = 0;
    for (i = 0; i < b; i++){
        if (LSH1[i] == LSH2[i]){
            num_similar++;
        }
    }

    return num_similar > similarity_threshold;
}

bool isSame(float *LSH1, float *LSH2)
{
    int i = 0;
    for (i = 0; i < b; i++){
        if (LSH1[i] != LSH2[i])
            return false;
    }
    return true;
}

void detectOutliers()
{
    int i, j, k, l;

    //printf("generate hyperplane!\n");

    //generate a random hyperplane r (bxm)
    float **r = (float **)malloc(sizeof(float *)*b);
    //printf("R:\n\t");
    for (i = 0; i < b; i++){
        r[i] = (float *)malloc(sizeof(float)*m);
        for (j = 0; j < m; j++){
            r[i][j] = uniform(-1.0, 1.0, 1);
            //printf("%f ", r[i][j]);
        }
        //printf("\n\t");
    }


    //printf("start PHASE ONE\n");
    //PHASE ONE ---------------------------------------------
    //generate a b-bit LSH for each node based on its last m readings
    for (i = 0; i < num_clusters; i++){
        for (j = 0; j < num_nodes; j++){
            Node *node = nodes[i][j];
            //node->LSH = (float *)malloc(sizeof(float)*b);
            node->duplicate = false;

            //printf("node: i: %d, j: %d, LSH: ", i, j);

            //use hash function u x r >= 0 -> 1, else 0
            //to generate b-bit LSH
            for (k = 0; k < b; k++){
                float lsh_temp = 0;
                for (l = 0; l < m; l++){
                    lsh_temp += node->readings[l] * r[k][l];
                }
                //printf("lsh temp?: %f\n", lsh_temp);
                if (lsh_temp >= 0)
                    node->LSH[k] = 1;
                else node->LSH[k] = 0;
            //    printf("%g", node->LSH[k]);
            }
            //printf("\t");
        /*    for (k = 0; k < m; k++){
                printf("%g, ", node->readings[k]);
            }*/
        //    printf("\n");
        }
    }
    //free the random hyperplane as our LSHs have been generated

    //printf("start PHASE TWO\n");
    //PHASE TWO ----------------------------------------------
    //Detect outliers and eliminate redundant data
    //keep track of which nodes are counted as "outliers"

    int true_positives = 0;  //detecting a faulty node correctly
    int false_positives = 0; //detecting a non-faulty node as faulty
    int false_negatives = 0; //NOT detecting a faulty node
    int num_outliers = 0;
    int num_true_outliers = 0;

    Node ***outlier_nodes = (Node ***)malloc(sizeof(Node **)*num_clusters);
    for (i = 0; i < num_clusters; i++){
        //printf("i: %d\n", i);
        outlier_nodes[i] = (Node **)malloc(sizeof(Node *)*num_nodes);
        //printf("!");
        int node_index = 0;
        for (j = 0; j < num_nodes; j++){
            //printf("\tj: %d\n", j);
            outlier_nodes[i][j] = NULL;

            Node *node1 = nodes[i][j];
            node1->support = 0;
            node1->duplicate = false;
            node1->is_outlier = false;
            if (node1->is_faulty)
                num_true_outliers++;

            //check for similarity!! ('eleminate outliers')
            for (k = 0; k < num_nodes; k++){
                if (k == j) continue;
                Node *node2 = nodes[i][k];

                if (isSimilar(node1->LSH, node2->LSH)){
                    node1->support++;
                }
                if (isSame(node1->LSH, node2->LSH) && !node2->duplicate){
                    node1->duplicate = true;
                }
            }
            //printf("\t\tsupport: %d\n", node1->support);
            //printf("node1 support: %d\n", node1.support);
            //if it's a local outlier, add it to the cluster's list of local outliers!!
            if (node1->support < min_sup_local)
            {
                outlier_nodes[i][node_index] = node1;
                node_index++;
                num_outliers++;
            }else{
                //we didn't detect a faulty node as an outlier!
                if (node1->is_faulty)
                    false_negatives++;
            }
        }
    }
    /*if (num_outliers > 0){
        printf("temp outliers: %d\n", num_outliers);
        printf("true outliers: %d\n", num_true_outliers);
    }*/
    //num_outliers = 0;
    //printf("done with the first phase 2 loop bay bay\n");
    //now, look through the local outliers of each cluster to determine if we can
    //incraese their support count
    for (i = 0; i < num_clusters; i++){
        int node_index = 0;
        for (j = 0; j < num_nodes; j++){
            if (outlier_nodes[i][j] == NULL) break;

            Node *outlier1 = outlier_nodes[i][j];
            outlier1->support = 0; //TODO:: ??
            for (k = 0; k < num_clusters; k++){
                if (k == i) continue;
                for (l = 0; l < num_nodes; l++){
                    if (outlier_nodes[k][l] == NULL) break;
                    Node *outlier2 = outlier_nodes[k][l];
                    if (isSimilar(outlier1->LSH, outlier2->LSH)){
                        outlier1->support++;
                    }
                }
            }

            //add to the true outlier list!!
            if (outlier1->support < min_sup_group){
                //we correctly identified a faulty node!
                if (outlier1->is_faulty)
                    true_positives++;
                //we incorrectly identified a node as faulty
                else false_positives++;

                outlier1->is_outlier = true;
                num_outliers++;
            }else{
                //we didn't detect a faulty node as a (true) outlier!
                if (outlier1->is_faulty)
                    false_negatives++;
            }
        }
    }
    //printf("true outliers: %d\n", num_outliers);
    //free the temp outlier nodes cuz we ain't need em!
    for (i = 0; i < num_clusters; i++){
        free(outlier_nodes[i]);
    }
    free(outlier_nodes);
    //
    // if (num_outliers > 0){
    //     printf("R:\n\t");
    //     for (i = 0; i < b; i++){
    //         //r[i] = (float *)malloc(sizeof(float)*m);
    //         for (j = 0; j < m; j++){
    //             //r[i][j] = uniform(-1.0, 1.0, 1);
    //             printf("%f ", r[i][j]);
    //         }
    //         printf("\n\t");
    //     }
    // }
    free(r);

    //printf("start PHASE THREE\n");
    //PHASE THREE -------------------------------------------
    //Actually aggregate the data from eliminations above??
    int num_bits_sent = 0;

    for (i = 0; i < num_clusters; i++){
        for (j = 0; j < num_nodes; j++){
            Node *node = nodes[i][j];
            if (node->duplicate || node->is_outlier) continue;

            //'send' the data!!! (we've sent b bits)
            num_bits_sent += b;
        }
    }

    //now free the LSH as we only needed it for this detection!
    for (i = 0; i < num_clusters; i++){
        for (j = 0; j < num_nodes; j++){
            Node *node = nodes[i][j];
        }
    }

    printf("true positives:  %d\n", true_positives);
    printf("false positives: %d\n", false_positives);
    printf("false negatives: %d\n", false_negatives);
    printf("num bits sent:   %d\n\n", num_bits_sent);
}
