#!/usr/bin/env python3
"""
Add missing shell system nodes to the UML canvas.

This script adds EditorShell, CommandShell, CodeShell, and CLIShell nodes
with proper titles, descriptions, and methods extracted from header files.
"""

import json
import re
import uuid
from pathlib import Path
from typing import Dict, List, Optional

# Canvas file path
CANVAS_PATH = Path.home() / "works" / "notes" / "Programming" / "videoTracker" / "videoTracker UML Diagram.canvas"

# Source directory
SRC_DIR = Path("/Users/jaufre/works/of_v0.12.1_osx_release/apps/myApps/videoTracker/src")

# Shell class definitions with descriptions and methods
SHELL_CLASSES = {
    "Shell": {
        "description": "Base class for different UI interaction modes",
        "is_abstract": True,
        "file": "shell/Shell.h",
        "methods": ["setup", "update", "draw", "exit", "handleKeyPress", "handleMousePress", 
                   "handleMouseDrag", "handleMouseRelease", "handleWindowResize", 
                   "setActive", "isActive", "getName", "getDescription"]
    },
    "EditorShell": {
        "description": "Wraps existing ImGui-based editor interface with tiled windows",
        "is_abstract": False,
        "file": "shell/EditorShell.h",
        "methods": ["setup", "update", "draw", "exit", "handleKeyPress", "handleMousePress", 
                   "handleWindowResize", "setDrawGUICallback", "setHandleKeyPressCallback"]
    },
    "CommandShell": {
        "description": "Custom-rendered terminal interface with REPL (Hydra/Strudel style)",
        "is_abstract": False,
        "file": "shell/CommandShell.h",
        "methods": ["setup", "update", "draw", "exit", "handleKeyPress", "handleMousePress",
                   "handleMouseDrag", "handleMouseRelease", "handleWindowResize",
                   "setEmbeddedMode", "isEmbeddedMode", "setEmbeddedBounds", "appendOutput"]
    },
    "CodeShell": {
        "description": "Live-coding shell with code editor and REPL (Strudel/Tidal/Hydra style)",
        "is_abstract": False,
        "file": "shell/CodeShell.h",
        "methods": ["setup", "update", "draw", "exit", "handleKeyPress", "handleMousePress",
                   "handleMouseDrag", "handleMouseRelease", "handleWindowResize",
                   "refreshScriptFromState", "executeSelection", "executeAll"]
    },
    "CLIShell": {
        "description": "Batch CLI mode for non-interactive command execution",
        "is_abstract": False,
        "file": "shell/CLIShell.h",
        "methods": ["setup", "update", "draw", "exit", "handleKeyPress", "executeCommand",
                   "executeFromStdin", "executeFromFile", "shouldExit"]
    }
}

# Node dimensions
ABSTRACT_WIDTH = 320
ABSTRACT_HEIGHT = 140
REGULAR_WIDTH = 280
REGULAR_HEIGHT = 100
NODE_SPACING_Y = 150  # Vertical spacing between shell nodes


def extract_public_methods(header_path: Path) -> List[str]:
    """Extract public methods from a C++ header file"""
    if not header_path.exists():
        return []
    
    try:
        content = header_path.read_text(encoding='utf-8')
    except Exception:
        return []
    
    methods = []
    in_public_section = False
    in_class = False
    brace_depth = 0
    
    # Remove comments and strings
    content_no_comments = re.sub(r'//.*?$', '', content, flags=re.MULTILINE)
    content_no_comments = re.sub(r'/\*.*?\*/', '', content_no_comments, flags=re.DOTALL)
    
    lines = content_no_comments.split('\n')
    
    for line in lines:
        # Detect class start
        if re.search(r'\bclass\s+\w+', line) and '{' in line:
            in_class = True
            in_public_section = False
            brace_depth = line.count('{') - line.count('}')
            continue
        
        if not in_class:
            continue
        
        # Track brace depth
        brace_depth += line.count('{') - line.count('}')
        
        if brace_depth <= 0:
            in_class = False
            continue
        
        # Detect public: section
        if re.search(r'\bpublic\s*:', line):
            in_public_section = True
            continue
        
        # Detect private: or protected: sections
        if re.search(r'\b(private|protected)\s*:', line):
            in_public_section = False
            continue
        
        # Extract method declarations
        if in_public_section:
            method_match = re.search(r'\b(\w+)\s*\([^)]*\)\s*(?:const\s*)?(?:override\s*)?(?:=\s*0\s*)?[;{]', line)
            if method_match:
                method_name = method_match.group(1)
                if method_name and method_name not in ['~', 'operator']:
                    if method_name not in methods:
                        methods.append(method_name)
    
    return methods


def get_key_methods(all_methods: List[str], class_info: dict) -> List[str]:
    """Select key interaction methods"""
    if not all_methods:
        # Use predefined methods if extraction failed
        return class_info.get("methods", [])[:8]
    
    # Prioritize interaction methods
    priority = []
    other = []
    
    for method in all_methods:
        method_lower = method.lower()
        if any(kw in method_lower for kw in ['handle', 'execute', 'setup', 'update', 'draw', 'set', 'get']):
            priority.append(method)
        else:
            other.append(method)
    
    result = priority[:6]
    if len(result) < 8:
        result.extend(other[:8 - len(result)])
    
    return result[:8]


def format_node_text(class_name: str, class_info: dict, methods: List[str], has_cpp: bool) -> str:
    """Format node text with title, description, and methods"""
    # Title with file extensions
    if has_cpp:
        title = f"# {class_name}.cpp/h"
    else:
        title = f"# {class_name}.h"
    
    lines = [title, ""]
    
    # Add description
    description = class_info.get("description", "")
    if description:
        lines.append(f"*{description}*")
        lines.append("")
    
    # Add methods
    if methods:
        lines.append("**Key Methods:**")
        for method in methods:
            lines.append(f"- `{method}()`")
    
    return "\n".join(lines)


def find_shell_node_position(canvas_data: dict) -> Optional[tuple]:
    """Find the position of the Shell node"""
    for node in canvas_data.get("nodes", []):
        if node.get("type") == "text":
            text = node.get("text", "")
            if re.search(r'#\s*Shell\.', text):
                return (node.get("x", 0), node.get("y", 0))
    return None


def generate_node_id() -> str:
    """Generate a node ID matching Obsidian format"""
    return uuid.uuid4().hex[:16]


def add_shell_nodes():
    """Add missing shell system nodes to canvas"""
    print(f"Reading canvas: {CANVAS_PATH}")
    
    # Read existing canvas
    with open(CANVAS_PATH, 'r', encoding='utf-8') as f:
        canvas_data = json.load(f)
    
    nodes = canvas_data.get("nodes", [])
    print(f"Found {len(nodes)} existing nodes")
    
    # Check which shell classes already exist
    existing_classes = set()
    for node in nodes:
        if node.get("type") == "text":
            text = node.get("text", "")
            match = re.search(r'#\s*(\w+)\.', text)
            if match:
                existing_classes.add(match.group(1))
    
    # Find Shell node position
    shell_pos = find_shell_node_position(canvas_data)
    if not shell_pos:
        print("⚠️  Shell node not found, using default position")
        shell_x, shell_y = 2140, -4320  # Default position based on canvas
    else:
        shell_x, shell_y = shell_pos
        print(f"Found Shell node at ({shell_x}, {shell_y})")
    
    # Determine which shells to add
    missing_shells = [name for name in SHELL_CLASSES.keys() if name not in existing_classes]
    print(f"\nMissing shell classes: {missing_shells}")
    
    # Add missing shell nodes
    new_nodes = []
    base_x = shell_x
    base_y = shell_y + 400  # Position below Shell node
    
    for i, class_name in enumerate(missing_shells):
        if class_name == "Shell":
            continue  # Shell already exists
        
        class_info = SHELL_CLASSES[class_name]
        
        # Check if cpp file exists
        header_path = SRC_DIR / class_info["file"]
        cpp_path = SRC_DIR / class_info["file"].replace(".h", ".cpp")
        has_cpp = cpp_path.exists()
        
        # Extract methods
        print(f"  Processing {class_name}...")
        all_methods = extract_public_methods(header_path)
        key_methods = get_key_methods(all_methods, class_info)
        
        # Format node text
        node_text = format_node_text(class_name, class_info, key_methods, has_cpp)
        
        # Determine size
        is_abstract = class_info.get("is_abstract", False)
        width = ABSTRACT_WIDTH if is_abstract else REGULAR_WIDTH
        height = ABSTRACT_HEIGHT if is_abstract else REGULAR_HEIGHT
        
        # Calculate position (stack vertically below Shell)
        x = base_x
        y = base_y + (i * NODE_SPACING_Y)
        
        # Create node
        new_node = {
            "id": generate_node_id(),
            "type": "text",
            "text": node_text,
            "styleAttributes": {},
            "x": float(x),
            "y": float(y),
            "width": float(width),
            "height": float(height)
        }
        
        new_nodes.append(new_node)
        print(f"    ✓ Added {class_name} at ({x}, {y})")
        if key_methods:
            print(f"      Found {len(key_methods)} key methods")
    
    # Add new nodes to canvas
    canvas_data["nodes"].extend(new_nodes)
    
    # Write updated canvas
    with open(CANVAS_PATH, 'w', encoding='utf-8') as f:
        json.dump(canvas_data, f, indent="\t", ensure_ascii=False)
    
    print(f"\n✅ Canvas updated successfully!")
    print(f"   Added {len(new_nodes)} new shell nodes")
    print(f"   Total nodes: {len(canvas_data['nodes'])}")


if __name__ == "__main__":
    add_shell_nodes()

