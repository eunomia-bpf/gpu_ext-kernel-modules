#!/usr/bin/env python3
import os
import re
import argparse
from collections import defaultdict

# Regex to capture function names from cflow output. Example:
#   +-uvm_parent_gpu_service_replayable_faults() <...>
# Captures: uvm_parent_gpu_service_replayable_faults
CFLOW_FUNC_PATTERN = re.compile(r'(\s*)\S*\s*([a-zA-Z0-9_.]+)\(\)')

# Regex to capture function counts from user-provided counter file. Example:
# @calls[kprobe:uvm_parent_gpu_service_replayable_faults]: 871
# Captures: uvm_parent_gpu_service_replayable_faults and 871
COUNTER_FUNC_PATTERN = re.compile(r'@calls\[kprobe:([a-zA-Z0-9_.]+)\]: (\d+)')


def parse_counter_file(input_file):
    """
    Parses the user-provided counter file to extract function names and their counts.
    Returns a list of target functions and a dictionary of counts.
    """
    counts = {}
    target_functions = []
    
    if not os.path.exists(input_file):
        print(f"Warning: Counter file not found: {input_file}")
        return target_functions, counts
        
    with open(input_file, 'r') as f:
        for line in f:
            match = COUNTER_FUNC_PATTERN.search(line)
            if match:
                func_name = match.group(1)
                count = int(match.group(2))
                counts[func_name] = count
                target_functions.append(func_name)
    return target_functions, counts


def parse_cflow_files_and_build_graph(cflow_dir):
    """
    Parses all .txt files in a directory to build a comprehensive call graph.
    Returns a dictionary where keys are function names and values are lists of their children's names.
    """
    graph = defaultdict(list)
    
    if not os.path.isdir(cflow_dir):
        print(f"Warning: cflow directory not found: {cflow_dir}")
        return graph
        
    cflow_files = [os.path.join(cflow_dir, f) for f in os.listdir(cflow_dir) if f.endswith('.txt')]

    for cflow_file in cflow_files:
        with open(cflow_file, 'r') as f:
            lines = f.readlines()
        
        # Stack to keep track of (function_name, indent_level)
        parent_stack = []
        
        for line in lines:
            match = CFLOW_FUNC_PATTERN.match(line)
            if not match:
                continue

            indent_len = len(match.group(1))
            func_name = match.group(2)
            
            # Pop from stack until the parent's indent is less than the current node's
            while parent_stack and indent_len <= parent_stack[-1][1]:
                parent_stack.pop()

            if parent_stack:
                parent_name = parent_stack[-1][0]
                # Avoid adding the same child multiple times from different call sites
                if func_name not in graph[parent_name]:
                    graph[parent_name].append(func_name)
            
            parent_stack.append((func_name, indent_len))
    return graph


def print_call_graph(start_node, graph, counts, indent_level=0, visited=None):
    """
    Recursively prints the call graph starting from a given node.
    """
    if visited is None:
        visited = set()

    # Print current node
    count_str = counts.get(start_node, 'N/A')
    indent = '    ' * indent_level
    cycle_indicator = " (Cycle Detected)" if start_node in visited else ""
    print(f"{indent}{start_node}() - Count: {count_str}{cycle_indicator}")

    if start_node in visited:
        return
        
    visited.add(start_node)

    # Recursively print children
    if start_node in graph:
        for child_node in graph[start_node]:
            print_call_graph(child_node, graph, counts, indent_level + 1, visited)
    
    visited.remove(start_node)


def main():
    """
    Main function to process cflow and counter files.
    """
    parser = argparse.ArgumentParser(description="Process a counter file and cflow output to generate an annotated call graph.")
    parser.add_argument("counter_file", help="Path to a text file containing bpftrace-style function counts.")
    parser.add_argument("--cflow-dir", default="/home/yunwei37/workspace/gpu/open-gpu-kernel-modules/docs/cflow_output", help="Directory containing cflow output files.")
    
    args = parser.parse_args()

    # 1. Parse the counter file for target function names and their counts
    target_functions, call_counts = parse_counter_file(args.counter_file)
    
    if not target_functions:
        print("No functions found in the counter file.")
        return

    # 2. Parse all cflow files to build a complete call graph
    call_graph_map = parse_cflow_files_and_build_graph(args.cflow_dir)
    
    if not call_graph_map:
        print("No call graph data found in the cflow directory.")
        return

    # 3. For each target function, find it in the graph and print its subgraph
    for func_name in target_functions:
        print("-" * 80)
        print(f"Call graph for: {func_name}\n")
        print_call_graph(func_name, call_graph_map, call_counts)
        print("-" * 80)
        print()

if __name__ == "__main__":
    main()
