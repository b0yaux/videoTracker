#!/usr/bin/env python3
"""
Update existing Obsidian canvas file with missing nodes and edges.

This script:
1. Reads the existing canvas file
2. Identifies missing classes
3. Adds missing nodes in appropriate positions
4. Adds inheritance and relationship edges
5. Preserves existing format (styleAttributes, etc.)
"""

import json
import uuid
from pathlib import Path
from typing import Dict, List, Set, Optional

# Canvas file path
CANVAS_PATH = Path.home() / "works" / "notes" / "Programming" / "videoTracker" / "videoTracker UML Diagram.canvas"

# All classes that should exist
ALL_CLASSES = {
    # Application
    "ofBaseApp", "ofApp",
    # Core Systems
    "Clock", "ModuleFactory", "ModuleRegistry", "ConnectionManager",
    "ParameterRouter", "SessionManager", "ProjectManager",
    "AudioRouter", "VideoRouter", "EventRouter", "CommandExecutor",
    "Engine", "EngineState", "PatternRuntime", "ScriptManager",
    # Module Base
    "Module",
    # Modules
    "TrackerSequencer", "MultiSampler", "MediaPlayer", "VoiceProcessor",
    "AudioMixer", "VideoMixer", "AudioOutput", "VideoOutput",
    "Oscilloscope", "Spectrogram",
    # GUI Base
    "ModuleGUI",
    # GUI Classes
    "GUIManager", "ViewManager", "TrackerSequencerGUI", "MultiSamplerGUI",
    "AudioMixerGUI", "VideoMixerGUI", "AudioOutputGUI", "VideoOutputGUI",
    "OscilloscopeGUI", "SpectrogramGUI", "ClockGUI", "MenuBar",
    "Console", "CommandBar", "FileBrowser", "AssetLibraryGUI", "AddMenu",
    # Cell System
    "BaseCell", "NumCell", "BoolCell", "MenuCell", "ParameterCell", "CellGrid",
    # Data Structures
    "Pattern", "PatternChain", "SampleRef", "Voice", "ParameterDescriptor",
    "Port", "TriggerEvent", "Envelope", "VoiceManager",
    # Utilities
    "AssetLibrary", "MediaConverter", "InputRouter", "ExpressionParser"
}

# Inheritance relationships
INHERITANCE = {
    "ofApp": "ofBaseApp",
    "TrackerSequencer": "Module",
    "MultiSampler": "Module",
    "AudioMixer": "Module",
    "VideoMixer": "Module",
    "AudioOutput": "Module",
    "VideoOutput": "Module",
    "Oscilloscope": "Module",
    "Spectrogram": "Module",
    "TrackerSequencerGUI": "ModuleGUI",
    "MultiSamplerGUI": "ModuleGUI",
    "AudioMixerGUI": "ModuleGUI",
    "VideoMixerGUI": "ModuleGUI",
    "AudioOutputGUI": "ModuleGUI",
    "VideoOutputGUI": "ModuleGUI",
    "OscilloscopeGUI": "ModuleGUI",
    "SpectrogramGUI": "ModuleGUI",
    "NumCell": "BaseCell",
    "BoolCell": "BaseCell",
    "MenuCell": "BaseCell",
}

# Composition relationships
COMPOSITIONS = {
    "ofApp": ["Clock", "ModuleFactory", "ModuleRegistry", "ConnectionManager", 
              "ParameterRouter", "SessionManager", "ProjectManager", "GUIManager",
              "ViewManager", "InputRouter", "AssetLibrary", "MediaConverter"],
    "ConnectionManager": ["AudioRouter", "VideoRouter", "EventRouter"],
    "MultiSampler": ["SampleRef", "Voice", "VoiceManager"],
    "TrackerSequencer": ["Pattern", "PatternChain"],
}

# Association relationships
ASSOCIATIONS = {
    "ModuleRegistry": ["Module"],
    "ModuleFactory": ["Module"],
    "ConnectionManager": ["Module"],
    "ParameterRouter": ["Module"],
    "SessionManager": ["Module"],
    "ModuleGUI": ["Module"],
    "ParameterCell": ["Module", "BaseCell"],
}

# Layer positions (matching existing layout)
LAYER_POSITIONS = {
    "application": {"x": -900, "y": -700},
    "core": {"x": -120, "y": -1000},
    "module_base": {"x": 980, "y": -900},
    "modules": {"x": 1080, "y": -920},
    "gui_base": {"x": 2000, "y": -570},
    "gui_classes": {"x": 2100, "y": -600},
    "cell_system": {"x": 3000, "y": -400},
    "data_structures": {"x": 3500, "y": -400},
    "utilities": {"x": 4000, "y": -400},
}

# Class to layer mapping
CLASS_TO_LAYER = {
    "ofBaseApp": "application", "ofApp": "application",
    "Clock": "core", "ModuleFactory": "core", "ModuleRegistry": "core",
    "ConnectionManager": "core", "ParameterRouter": "core", "SessionManager": "core",
    "ProjectManager": "core", "AudioRouter": "core", "VideoRouter": "core",
    "EventRouter": "core", "CommandExecutor": "core", "Engine": "core",
    "EngineState": "core", "PatternRuntime": "core", "ScriptManager": "core",
    "Module": "module_base",
    "TrackerSequencer": "modules", "MultiSampler": "modules", "MediaPlayer": "modules",
    "VoiceProcessor": "modules", "AudioMixer": "modules", "VideoMixer": "modules",
    "AudioOutput": "modules", "VideoOutput": "modules", "Oscilloscope": "modules",
    "Spectrogram": "modules",
    "ModuleGUI": "gui_base",
    "GUIManager": "gui_classes", "ViewManager": "gui_classes",
    "TrackerSequencerGUI": "gui_classes", "MultiSamplerGUI": "gui_classes",
    "AudioMixerGUI": "gui_classes", "VideoMixerGUI": "gui_classes",
    "AudioOutputGUI": "gui_classes", "VideoOutputGUI": "gui_classes",
    "OscilloscopeGUI": "gui_classes", "SpectrogramGUI": "gui_classes",
    "ClockGUI": "gui_classes", "MenuBar": "gui_classes", "Console": "gui_classes",
    "CommandBar": "gui_classes", "FileBrowser": "gui_classes",
    "AssetLibraryGUI": "gui_classes", "AddMenu": "gui_classes",
    "BaseCell": "cell_system", "NumCell": "cell_system", "BoolCell": "cell_system",
    "MenuCell": "cell_system", "ParameterCell": "cell_system", "CellGrid": "cell_system",
    "Pattern": "data_structures", "PatternChain": "data_structures",
    "SampleRef": "data_structures", "Voice": "data_structures",
    "ParameterDescriptor": "data_structures", "Port": "data_structures",
    "TriggerEvent": "data_structures", "Envelope": "data_structures",
    "VoiceManager": "data_structures",
    "AssetLibrary": "utilities", "MediaConverter": "utilities",
    "InputRouter": "utilities", "ExpressionParser": "utilities",
}


def get_existing_nodes(canvas_data: dict) -> Dict[str, dict]:
    """Extract existing nodes by class name"""
    existing = {}
    for node in canvas_data.get("nodes", []):
        if node.get("type") == "text":
            text = node.get("text", "").strip()
            # Clean up text (remove "(abstract)", file extensions, etc.)
            class_name = text.replace(" (abstract)", "").replace(".h", "").replace(".cpp/h", "").replace(".cpp", "").strip()
            existing[class_name] = node
    return existing


def generate_node_id() -> str:
    """Generate a node ID matching Obsidian format"""
    return uuid.uuid4().hex[:16]


def create_missing_node(class_name: str, layer: str, existing_nodes: Dict[str, dict]) -> Optional[dict]:
    """Create a new node for a missing class"""
    layer_pos = LAYER_POSITIONS.get(layer, {"x": 0, "y": 0})
    
    # Find position relative to existing nodes in same layer
    existing_in_layer = [n for n in existing_nodes.values() 
                        if CLASS_TO_LAYER.get(n.get("text", "").replace(" (abstract)", "").replace(".h", "").replace(".cpp/h", "").strip(), "") == layer]
    
    # Determine if abstract
    is_abstract = class_name in ["Module", "ModuleGUI", "BaseCell", "ofBaseApp"]
    
    # Calculate position based on existing layout
    x_base = layer_pos["x"]
    y_base = layer_pos["y"]
    
    # Count how many nodes already in this layer
    count = len(existing_in_layer)
    
    # Get existing Y positions in this layer to avoid overlaps
    existing_y_positions = sorted([n.get("y", 0) for n in existing_in_layer])
    
    # Position new node
    if layer == "core":
        # Grid layout (2 columns) - match existing pattern
        col = count % 2
        row = count // 2
        x = x_base + col * 340
        y = y_base + row * 80
    elif layer == "modules":
        # Grid layout (2 columns) - match existing pattern
        col = count % 2
        row = count // 2
        x = x_base + col * 280
        y = y_base + row * 80
    elif layer == "gui_classes":
        # Grid layout (2 columns) - match existing pattern
        col = count % 2
        row = count // 2
        x = x_base + col * 280
        y = y_base + row * 80
    elif layer == "application":
        # Stack vertically below existing
        x = x_base
        y = y_base - count * 100  # Go up (negative Y)
    elif layer == "module_base":
        # Position near Module.h
        x = x_base
        y = y_base
    elif layer == "gui_base":
        # Position near ModuleGUI.h
        x = x_base
        y = y_base
    elif layer == "cell_system":
        # Stack to the right
        x = x_base + count * 300
        y = y_base
    elif layer == "data_structures":
        # Stack vertically
        x = x_base
        y = y_base + count * 100
    elif layer == "utilities":
        # Stack vertically
        x = x_base
        y = y_base + count * 100
    else:
        # Default: vertical stack
        x = x_base
        y = y_base + count * 100
    
    # Format text
    text = f"*{class_name}*\n(abstract)" if is_abstract else class_name
    
    return {
        "id": generate_node_id(),
        "type": "text",
        "text": text,
        "styleAttributes": {},
        "x": x,
        "y": y,
        "width": 300 if is_abstract else 260,
        "height": 200 if is_abstract else 60
    }


def create_edges(existing_nodes: Dict[str, dict], canvas_data: dict) -> List[dict]:
    """Create inheritance and relationship edges"""
    edges = []
    edge_id = 0
    
    # Helper to get node ID by class name
    def get_node_id(class_name: str) -> Optional[str]:
        for name, node in existing_nodes.items():
            clean_name = name.replace(" (abstract)", "").replace(".h", "").replace(".cpp/h", "").replace(".cpp", "").strip()
            if clean_name == class_name:
                return node.get("id")
        return None
    
    # Inheritance edges
    for child, parent in INHERITANCE.items():
        if parent:  # Skip if no parent
            child_id = get_node_id(child)
            parent_id = get_node_id(parent)
            if child_id and parent_id:
                edges.append({
                    "id": f"edge-{edge_id}",
                    "fromNode": child_id,
                    "fromSide": "top",
                    "toNode": parent_id,
                    "toSide": "bottom",
                    "color": "2"
                })
                edge_id += 1
    
    # Composition edges (only key relationships to avoid clutter)
    key_compositions = {
        "ofApp": ["ModuleRegistry", "ConnectionManager", "GUIManager"],
        "ConnectionManager": ["AudioRouter", "VideoRouter", "EventRouter"],
    }
    
    for from_class, to_classes in key_compositions.items():
        from_id = get_node_id(from_class)
        if from_id:
            for to_class in to_classes:
                to_id = get_node_id(to_class)
                if to_id:
                    edges.append({
                        "id": f"edge-{edge_id}",
                        "fromNode": from_id,
                        "fromSide": "right",
                        "toNode": to_id,
                        "toSide": "left",
                        "color": "3"
                    })
                    edge_id += 1
    
    # Key associations
    key_associations = {
        "ModuleRegistry": ["Module"],
        "ModuleGUI": ["Module"],
    }
    
    for from_class, to_classes in key_associations.items():
        from_id = get_node_id(from_class)
        if from_id:
            for to_class in to_classes:
                to_id = get_node_id(to_class)
                if to_id and from_id != to_id:
                    edges.append({
                        "id": f"edge-{edge_id}",
                        "fromNode": from_id,
                        "fromSide": "right",
                        "toNode": to_id,
                        "toSide": "left",
                        "color": "4"
                    })
                    edge_id += 1
    
    return edges


def main():
    """Update existing canvas with missing nodes and edges"""
    print(f"Reading canvas: {CANVAS_PATH}")
    
    # Read existing canvas
    with open(CANVAS_PATH, 'r', encoding='utf-8') as f:
        canvas_data = json.load(f)
    
    # Get existing nodes
    existing_nodes = get_existing_nodes(canvas_data)
    print(f"Found {len(existing_nodes)} existing nodes")
    
    # Find missing classes
    existing_class_names = set(existing_nodes.keys())
    missing_classes = ALL_CLASSES - existing_class_names
    
    print(f"Missing classes: {len(missing_classes)}")
    for cls in sorted(missing_classes):
        print(f"  - {cls}")
    
    # Add missing nodes
    new_nodes = []
    for class_name in missing_classes:
        layer = CLASS_TO_LAYER.get(class_name, "utilities")
        new_node = create_missing_node(class_name, layer, existing_nodes)
        if new_node:
            new_nodes.append(new_node)
            existing_nodes[class_name] = new_node
            print(f"Added: {class_name} at ({new_node['x']}, {new_node['y']})")
    
    # Add new nodes to canvas
    canvas_data["nodes"].extend(new_nodes)
    
    # Remove all edges (user requested no links)
    print("\nRemoving all edges...")
    canvas_data["edges"] = []
    print("All edges removed")
    
    # Preserve metadata
    if "metadata" not in canvas_data:
        canvas_data["metadata"] = {
            "version": "1.0-1.0",
            "frontmatter": {}
        }
    
    # Write updated canvas
    with open(CANVAS_PATH, 'w', encoding='utf-8') as f:
        json.dump(canvas_data, f, indent="\t", ensure_ascii=False)
    
    print(f"\n✅ Canvas updated successfully!")
    print(f"   Total nodes: {len(canvas_data['nodes'])}")
    print(f"   Total edges: {len(canvas_data['edges'])} (removed)")
    print(f"   Added {len(new_nodes)} new nodes")
    print(f"   No edges created (as requested)")


if __name__ == "__main__":
    main()

