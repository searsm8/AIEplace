import json
import os
from copy import deepcopy

def readJSON(filepath, file_number):
    with open(filepath) as file:
        JSON = json.load(file)
        node_names = list(JSON["partition"])
        parameters = (JSON["switchbox01"])
        for herd in range(len(node_names)):
            for tile in range(2, len(node_names[herd])):
                node_names[herd][tile][1] += file_number * (parameters["col"] +1)
        return node_names

def create_super_list(directory):
    files = []
    for filename in os.listdir(directory):
        f = os.path.join(directory, filename)
        # checking if it is a file
        if os.path.isfile(f):
            print(f)
            files.append(f)
    i = 0
    super_node_list = []
    files = sorted(files)
    for file in files:
        super_node_list += readJSON(file, i)
        i += 1
    num_rows = 0
    num_cols = 0
    with open(files[0]) as file:
        JSON = json.load(file)
        parameters = (JSON["switchbox01"])
        num_rows = parameters["row"]
        num_cols = parameters["col"]
    # adding 1 because it's subtracted later
    num_cols += 1
    num_cols *= len(files)
    num_rows += 1
    return super_node_list, num_rows, num_cols
