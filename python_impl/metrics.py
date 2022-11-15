import math
import json
import os
import csv
from AIEplacer import Design
from copy import deepcopy
from statistics import mean

def printMetrics(JSON_input, JSON_output, method_label="NO_METHOD"):

    print("\n###############")
    print("### METRICS ###")
    print("###############")
    # Read in the JSON file, create a Grid and Design
    with open(JSON_output) as file:
        design, grid, orig_num_cols, _ = Design.readJSON(JSON_input)
        JSON = json.load(file)


        print("Timeslot execution times:")
        nodes_by_timeslot = []*grid.num_timeslots
        for i in range(grid.num_timeslots): nodes_by_timeslot.append([])

        nodes = JSON["partition"]
        for node in nodes:
            slot = math.floor(node[2][1] / grid.timeslot_cols)
            nodes_by_timeslot[slot].append(design.node_names.index(node[1]))
            
        # For each time slot, find the longest "chain" execution time.
        # If a node has a predecessor within the same timeslot, their 
        # execution times must be added together
        timeslot_area = grid.num_rows * grid.timeslot_cols
        execution_times = [0]*len(design.node_names)
        stats = []
        for slot in range(grid.num_timeslots):
            print(f"\n### Execution times for timeslot {slot}")
            for i in nodes_by_timeslot[slot]:
                execution_times[i] = (getExecutionTimeOfNode(design, i, nodes_by_timeslot[slot]))
            stats.append(computeStatsForSlot(design, nodes_by_timeslot[slot], execution_times))
            t_avg, t_max, area = stats[-1]
            print(f"### Timeslot execution time: {t_max}\tAvg execution time: {t_avg:.2f}\tTime Efficiency: {t_avg/t_max:.2f}")
            print(f"### Area: {area}\tTimeslot area: {timeslot_area}\tArea Utilization: {area/timeslot_area}")
            print(f"##################################################################")
            #print(f"Slot {slot}: {[design.node_names[i] for i in nodes_by_timeslot[slot]]}")
            #print(f"\tExecution times: {[design.times[i] for i in nodes_by_timeslot[slot]]}")
            #print(f"Worst execution time for slot {slot} is {worst_times[-1]}")
        #min_col, max_col = grid.getColsInTimeslot(timeslot_num)
        #for slot in range(grid.num_timeslots):
        #    print(f"Stats for Timeslot {slot}:")
        #    print(f"{stats[slot]}")
        overall_exec_eff  = mean([stats[i][0]/stats[i][1] for i in range(len(stats))])
        overall_area_util = mean([stats[i][2]/timeslot_area for i in range(len(stats))])
        print(f"\nOverall execution time efficiency: {overall_exec_eff*100:.1f}%")
        print(f"Overall Area utilization: {overall_area_util*100:.1f}%")
        print("")
        print(f"##################################################################")

        # Write results to csv
        # If file doesn't exist yet, create it and write the header
        if(not os.path.exists(f"csv/{method_label}.csv")):
            os.system(f"mkdir -p csv/")
            with open(f'csv/{method_label}.csv', 'w', encoding='UTF8') as f:
                writer = csv.writer(f)
                writer.writerow(["Benchmark", "Area Util", "Time Util"])
        
        # write the data
        with open(f'csv/{method_label}.csv', 'a', encoding='UTF8') as f:
            writer = csv.writer(f)
            writer.writerow([design.name, f'{overall_area_util:.3f}', f'{overall_exec_eff:.3f}'])
    # END printMetrics()


def computeStatsForSlot(design, nodes_in_slot, execution_times):
    if len(nodes_in_slot) == 0: return -1, -1, -1

    sum = max = area = 0
    for i in nodes_in_slot:
        sum += execution_times[i]
        size = design.node_sizes[i]
        area += size.row * size.col
        if execution_times[i] > max:
            max = execution_times[i]
    avg = sum / len(nodes_in_slot)
    return avg, max, area


def getExecutionTimeOfNode(design, index, nodes_in_timeslot):
    nodes = deepcopy(nodes_in_timeslot)
    t = design.times[index]
    #print(f"{design.node_names[index]}: t = {t}")
    if index in nodes:
        nodes.remove(index)
        longest_pred_time = 0
        for pred in design.predecessors[index]:
            if pred in nodes:
                pred_time = getExecutionTimeOfNode(design, pred, nodes)
                if pred_time > longest_pred_time:
                    longest_pred_time = pred_time
        t += longest_pred_time
    return t