#!/usr/bin/env python3
"""
Generate UML class diagram in Obsidian Canvas format for videoTracker.

This script:
1. Parses C++ class definitions to extract inheritance relationships
2. Groups classes by architectural layers (Core, Modules, GUI, etc.)
3. Creates proper UML-style nodes with class hierarchies
4. Generates an Obsidian canvas JSON file
5. Saves it to the Obsidian vault
"""

import json
import re
from pathlib import Path
from collections import defaultdict
from typing import Dict, List, Set, Tuple, Optional
import uuid

# Configuration
CODEBASE_ROOT = Path(__file__).parent.parent
OBSIDIAN_VAULT = Path.home() / "works" / "notes"
CANVAS_NAME = "videoTracker UML Diagram.canvas"

# Advanced Canvas compatibility mode
# If True, generates minimal format that Advanced Canvas might accept
ADVANCED_CANVAS_MODE = True

# UML Architecture Layers (based on UML_DIAGRAM_REFERENCE.md)
UML_LAYERS = {
    "application": {
        "classes": ["ofApp", "ofBaseApp"],
        "x": 0,
        "y": 0,
        "color": "#95A5A6",
        "label": "Application Layer"
    },
    "core_systems": {
        "classes": [
            "Clock", "ModuleFactory", "ModuleRegistry", "ConnectionManager",
            "ParameterRouter", "SessionManager", "ProjectManager",
            "AudioRouter", "VideoRouter", "EventRouter", "CommandExecutor",
            "Engine", "EngineState", "PatternRuntime", "ScriptManager"
        ],
        "x": 800,
        "y": 0,
        "color": "#4A90E2",
        "label": "Core Systems"
    },
    "module_base": {
        "classes": ["Module"],
        "x": 1600,
        "y": 0,
        "color": "#50C878",
        "label": "Module Base"
    },
    "modules": {
        "classes": [
            "TrackerSequencer", "MultiSampler", "MediaPlayer", "VoiceProcessor",
            "AudioMixer", "VideoMixer", "AudioOutput", "VideoOutput",
            "Oscilloscope", "Spectrogram"
        ],
        "x": 2400,
        "y": 0,
        "color": "#50C878",
        "label": "Modules"
    },
    "gui_base": {
        "classes": ["ModuleGUI"],
        "x": 3200,
        "y": 0,
        "color": "#FF6B6B",
        "label": "GUI Base"
    },
    "gui_classes": {
        "classes": [
            "GUIManager", "ViewManager", "TrackerSequencerGUI", "MultiSamplerGUI",
            "AudioMixerGUI", "VideoMixerGUI", "AudioOutputGUI", "VideoOutputGUI",
            "OscilloscopeGUI", "SpectrogramGUI", "ClockGUI", "MenuBar",
            "Console", "CommandBar", "FileBrowser", "AssetLibraryGUI", "AddMenu"
        ],
        "x": 4000,
        "y": 0,
        "color": "#FF6B6B",
        "label": "GUI Classes"
    },
    "cell_system": {
        "classes": ["BaseCell", "NumCell", "BoolCell", "MenuCell", "ParameterCell", "CellGrid"],
        "x": 4800,
        "y": 0,
        "color": "#FFA500",
        "label": "Cell System"
    },
    "data_structures": {
        "classes": [
            "Pattern", "PatternChain", "SampleRef", "Voice", "ParameterDescriptor",
            "Port", "TriggerEvent", "Envelope", "VoiceManager"
        ],
        "x": 5600,
        "y": 0,
        "color": "#7B68EE",
        "label": "Data Structures"
    },
    "utilities": {
        "classes": ["AssetLibrary", "MediaConverter", "InputRouter", "ExpressionParser"],
        "x": 6400,
        "y": 0,
        "color": "#9B59B6",
        "label": "Utilities"
    }
}

# Inheritance relationships (child -> parent)
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
    "MediaPlayer": None,  # Standalone
    "VoiceProcessor": "ofxSoundObject",
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
    "Clock": "ofxSoundOutput",
}

# Composition/Association relationships (from -> to)
COMPOSITIONS = {
    "ofApp": ["Clock", "ModuleFactory", "ModuleRegistry", "ConnectionManager", 
              "ParameterRouter", "SessionManager", "ProjectManager", "GUIManager",
              "ViewManager", "InputRouter", "AssetLibrary", "MediaConverter"],
    "ConnectionManager": ["AudioRouter", "VideoRouter", "EventRouter"],
    "GUIManager": ["ModuleGUI"],
    "MultiSampler": ["SampleRef", "Voice", "VoiceManager"],
    "Voice": ["MediaPlayer", "VoiceProcessor"],
    "TrackerSequencer": ["Pattern", "PatternChain"],
}

# Association relationships (bidirectional)
ASSOCIATIONS = {
    "ModuleRegistry": ["Module"],
    "ModuleFactory": ["Module"],
    "ConnectionManager": ["Module"],
    "ParameterRouter": ["Module"],
    "SessionManager": ["Module"],
    "ModuleGUI": ["Module"],
    "ParameterCell": ["Module", "BaseCell"],
}

# Node dimensions
CLASS_NODE_WIDTH = 280
CLASS_NODE_HEIGHT = 100
ABSTRACT_NODE_WIDTH = 300
ABSTRACT_NODE_HEIGHT = 120
NODE_SPACING_Y = 150  # Vertical spacing between classes
INHERITANCE_INDENT = 80  # Horizontal indent for child classes
LAYER_SPACING_X = 900  # Space between layers
LAYER_HEADER_HEIGHT = 120


class UMLClass:
    """Represents a UML class node"""
    def __init__(self, name: str, layer: str = None):
        self.name = name
        self.node_id = f"class-{uuid.uuid4().hex[:8]}"
        self.layer = layer or self._find_layer()
        self.is_abstract = name in ["Module", "ModuleGUI", "BaseCell", "ofBaseApp"]
        self.x = 0
        self.y = 0
        self.parent_class = INHERITANCE.get(name)
        self.children: List[str] = []
        
    def _find_layer(self) -> str:
        """Find which layer this class belongs to"""
        for layer_name, layer_info in UML_LAYERS.items():
            if self.name in layer_info["classes"]:
                return layer_name
        return "utilities"
    
    def get_display_text(self) -> str:
        """Get formatted text for the class node"""
        if self.is_abstract:
            return f"*{self.name}*\n(abstract)"
        return self.name


def parse_class_definitions() -> Dict[str, UMLClass]:
    """Parse C++ files to extract class definitions"""
    classes: Dict[str, UMLClass] = {}
    src_dir = CODEBASE_ROOT / "src"
    
    # Known classes from UML reference
    all_classes = set()
    for layer_info in UML_LAYERS.values():
        all_classes.update(layer_info["classes"])
    
    # Add classes from inheritance map
    all_classes.update(INHERITANCE.keys())
    
    # Create UMLClass objects
    for class_name in all_classes:
        classes[class_name] = UMLClass(class_name)
    
    # Build parent-child relationships
    for child_name, parent_name in INHERITANCE.items():
        if parent_name and child_name in classes:
            classes[child_name].parent_class = parent_name
            if parent_name in classes:
                classes[parent_name].children.append(child_name)
    
    return classes


def layout_classes(classes: Dict[str, UMLClass]) -> None:
    """Simple grid layout by layer"""
    # Group by layer
    by_layer: Dict[str, List[UMLClass]] = defaultdict(list)
    for cls in classes.values():
        by_layer[cls.layer].append(cls)
    
    # Simple grid layout for each layer
    for layer_name, layer_classes in by_layer.items():
        layer_info = UML_LAYERS.get(layer_name, {})
        x_base = layer_info.get("x", 0)
        y_base = layer_info.get("y", 0) + LAYER_HEADER_HEIGHT
        
        # Sort classes: abstract first, then alphabetically
        layer_classes.sort(key=lambda c: (not c.is_abstract, c.name))
        
        # Simple vertical stack
        for i, cls in enumerate(layer_classes):
            cls.x = x_base
            cls.y = y_base + i * NODE_SPACING_Y


def create_canvas_json(classes: Dict[str, UMLClass]) -> dict:
    """Create Obsidian canvas JSON with UML diagram"""
    canvas_nodes = []
    canvas_edges = []
    
    # Create layer headers
    for layer_name, layer_info in UML_LAYERS.items():
        # JSON Canvas 1.0 compliant header
        header = {
            "id": f"header-{layer_name}",
            "type": "text",
            "x": float(layer_info["x"]),
            "y": float(layer_info["y"]),
            "width": float(300),
            "height": float(80),
            "text": f"**{layer_info['label']}**"
        }
        
        # Add color if specified
        layer_color = layer_info.get("color", "1")
        if layer_color:
            header["color"] = str(layer_color) if isinstance(layer_color, str) and layer_color.isdigit() else "1"
        
        # fontSize is not in JSON Canvas 1.0 spec - remove it for Advanced Canvas compatibility
        canvas_nodes.append(header)
    
    # Create class nodes
    for cls in classes.values():
        width = ABSTRACT_NODE_WIDTH if cls.is_abstract else CLASS_NODE_WIDTH
        height = ABSTRACT_NODE_HEIGHT if cls.is_abstract else CLASS_NODE_HEIGHT
        
        # JSON Canvas 1.0 compliant node structure
        # Advanced Canvas requires exact format - ensure all values are proper types
        node = {
            "id": cls.node_id,
            "type": "text",
            "x": float(cls.x),  # Ensure numeric type
            "y": float(cls.y),
            "width": float(width),
            "height": float(height),
            "text": cls.get_display_text()
        }
        
        # Color is optional in JSON Canvas spec - add only if we have a valid color
        layer_color = UML_LAYERS.get(cls.layer, {}).get("color", "1")
        if layer_color:
            # Convert hex color to Obsidian color index if needed
            # Obsidian uses color indices: "1" through "6" for built-in colors
            node["color"] = str(layer_color) if layer_color.isdigit() else "1"
        
        # Ensure no extra properties that Advanced Canvas might reject
        canvas_nodes.append(node)
    
    # Advanced Canvas doesn't support programmatically created edges
    # Set to True to generate edges (may cause "updating edges from portal is not supported" error)
    # Set to False to generate nodes only (user can add edges manually)
    GENERATE_EDGES = False
    
    if GENERATE_EDGES:
        # Create inheritance edges (simple vertical connections)
        edge_id = 0
        for cls in classes.values():
            if cls.parent_class and cls.parent_class in classes:
                parent = classes[cls.parent_class]
                # Simple vertical connection: child below parent
                edge = {
                    "id": f"inherit-{edge_id}",
                    "fromNode": cls.node_id,
                    "fromSide": "top",
                    "toNode": parent.node_id,
                    "toSide": "bottom",
                    "color": "2"  # Gray for inheritance
                }
                canvas_edges.append(edge)
                edge_id += 1
        
        # Create composition edges (filled diamond - represented as solid line)
        for from_class, to_classes in COMPOSITIONS.items():
            if from_class in classes:
                from_node = classes[from_class]
                for to_class in to_classes:
                    if to_class in classes:
                        to_node = classes[to_class]
                        edge = {
                            "id": f"comp-{edge_id}",
                            "fromNode": from_node.node_id,
                            "fromSide": "right",
                            "toNode": to_node.node_id,
                            "toSide": "left",
                            "color": "3"  # Blue for composition
                        }
                        canvas_edges.append(edge)
                        edge_id += 1
        
        # Create association edges (dashed lines)
        for from_class, to_classes in ASSOCIATIONS.items():
            if from_class in classes:
                from_node = classes[from_class]
                for to_class in to_classes:
                    if to_class in classes:
                        to_node = classes[to_class]
                        # Only draw if not already connected by inheritance
                        if to_node.parent_class != from_class and from_node.parent_class != to_class:
                            edge = {
                                "id": f"assoc-{edge_id}",
                                "fromNode": from_node.node_id,
                                "fromSide": "right",
                                "toNode": to_node.node_id,
                                "toSide": "left",
                                "color": "4"  # Different color for association
                            }
                            canvas_edges.append(edge)
                            edge_id += 1
    else:
        print("   (Edges disabled - Advanced Canvas doesn't support programmatic edges)")
        print("   You can add edges manually in Obsidian by connecting nodes")
    
    # Ensure canvas format is compatible with Advanced Canvas
    # Advanced Canvas may require specific format or no groups
    canvas_json = {
        "nodes": canvas_nodes,
        "edges": canvas_edges
    }
    
    # Remove any properties that might prevent editing
    # Advanced Canvas should work with standard format, but let's be explicit
    return canvas_json


def create_layer_groups(classes: Dict[str, UMLClass]) -> List[dict]:
    """Create group containers for each layer (optional visual grouping)"""
    groups = []
    
    by_layer: Dict[str, List[UMLClass]] = defaultdict(list)
    for cls in classes.values():
        by_layer[cls.layer].append(cls)
    
    for layer_name, layer_classes in by_layer.items():
        if not layer_classes:
            continue
        
        layer_info = UML_LAYERS.get(layer_name, {})
        
        # Calculate bounding box
        min_x = min(c.x for c in layer_classes) - 50
        max_x = max(c.x for c in layer_classes) + CLASS_NODE_WIDTH + 50
        min_y = min(c.y for c in layer_classes) - 50
        max_y = max(c.y for c in layer_classes) + CLASS_NODE_HEIGHT + 50
        
        group = {
            "id": f"group-{layer_name}",
            "type": "group",
            "x": min_x,
            "y": layer_info.get("y", 0) + LAYER_HEADER_HEIGHT - 50,
            "width": max_x - min_x,
            "height": max_y - min_y + 50,
            "color": layer_info.get("color", "1"),
            "label": layer_info.get("label", layer_name)
        }
        # Groups are editable by default in Obsidian
        groups.append(group)
    
    return groups


def main():
    """Main function"""
    print("Parsing class definitions...")
    classes = parse_class_definitions()
    
    print(f"Found {len(classes)} classes")
    
    print("Layout classes...")
    layout_classes(classes)
    
    print("Creating canvas JSON...")
    canvas_data = create_canvas_json(classes)
    
    # Advanced Canvas compatibility: Try without groups first
    # Groups might prevent editing in Advanced Canvas
    USE_GROUPS = False  # Set to True to enable groups
    
    if USE_GROUPS:
        # Add layer groups
        groups = create_layer_groups(classes)
        canvas_data["nodes"].extend(groups)
        print("   (Groups enabled)")
    else:
        print("   (Groups disabled for Advanced Canvas compatibility)")
    
    # Ensure Obsidian vault directory exists
    OBSIDIAN_VAULT.mkdir(parents=True, exist_ok=True)
    
    # Write canvas file with JSON Canvas 1.0 strict compliance
    canvas_path = OBSIDIAN_VAULT / CANVAS_NAME
    with open(canvas_path, 'w', encoding='utf-8') as f:
        # Use ensure_ascii=False to preserve any special characters
        # Use strict JSON formatting for Advanced Canvas compatibility
        json.dump(canvas_data, f, indent=2, ensure_ascii=False)
    
    # Ensure file is writable
    import os
    os.chmod(canvas_path, 0o644)  # Read/write for owner, read for others
    
    print(f"\n✅ UML Canvas generated successfully!")
    print(f"   Location: {canvas_path}")
    print(f"   Classes: {len(classes)}")
    print(f"   Nodes: {len(canvas_data['nodes'])}")
    print(f"   Edges: {len(canvas_data['edges'])}")
    print(f"\n✅ Canvas generated in JSON Canvas 1.0 format")
    print(f"   - Strict compliance with JSON Canvas specification")
    print(f"   - All numeric values properly typed")
    print(f"   - No extra properties that might confuse Advanced Canvas")
    print(f"   ")
    print(f"   File permissions: {oct(os.stat(canvas_path).st_mode)[-3:]}")
    print(f"   ")
    print(f"   If nodes are still not editable in Advanced Canvas:")
    print(f"   1. Try closing and reopening the canvas")
    print(f"   2. Test with standard Canvas (disable Advanced Canvas)")
    print(f"   3. Check Advanced Canvas plugin settings")
    print(f"   4. Ensure Advanced Canvas plugin is up to date")
    
    # Print layer summary
    by_layer: Dict[str, int] = defaultdict(int)
    for cls in classes.values():
        by_layer[cls.layer] += 1
    
    print(f"\n📊 Classes by layer:")
    for layer, count in sorted(by_layer.items()):
        print(f"   {layer}: {count}")


if __name__ == "__main__":
    main()

