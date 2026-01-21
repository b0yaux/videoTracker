#!/usr/bin/env python3
"""
Update canvas node titles with file extensions and add key interaction methods.

This script:
1. Parses C++ header files to extract public methods
2. Updates node titles to "# ClassName.cpp/h" format
3. Adds key interaction methods to each node
"""

import json
import re
from pathlib import Path
from typing import Dict, List, Optional, Set

# Canvas file path
CANVAS_PATH = Path.home() / "works" / "notes" / "Programming" / "videoTracker" / "videoTracker UML Diagram.canvas"

# Source directory
SRC_DIR = Path("/Users/jaufre/works/of_v0.12.1_osx_release/apps/myApps/videoTracker/src")

# Class name to file mapping (for classes that don't follow standard naming)
CLASS_TO_FILE = {
    "ofApp": "ofApp",
    "ofBaseApp": "ofBaseApp",  # This is from openFrameworks, not in our codebase
    # Structs defined in other files
    "TriggerEvent": "modules/Module",  # Defined in Module.h
    "Port": "modules/Module",  # Defined in Module.h
    "SampleRef": "modules/MultiSampler",  # Defined in MultiSampler.h
    "Voice": "modules/MultiSampler",  # Defined in MultiSampler.h
}

# Methods that indicate interaction with other modules
INTERACTION_KEYWORDS = {
    "get", "set", "connect", "disconnect", "register", "add", "remove",
    "create", "execute", "load", "save", "subscribe", "unsubscribe",
    "route", "process", "update", "notify", "find", "query"
}

# Methods to exclude (too generic or internal)
EXCLUDE_METHODS = {
    "getType", "getName", "getInstanceName", "setInstanceName",
    "toJson", "fromJson", "serialize", "deserialize",
    "setup", "update", "draw", "audioOut", "videoOut",
    "getWidth", "getHeight", "setWidth", "setHeight",
    "getX", "getY", "setX", "setY", "getPosition", "setPosition",
    "getColor", "setColor", "isVisible", "setVisible",
    "getParent", "setParent", "getChildren", "addChild", "removeChild"
}


def find_class_file(class_name: str) -> Optional[Path]:
    """Find the header file for a class"""
    # Check explicit mapping first
    if class_name in CLASS_TO_FILE:
        base_name = CLASS_TO_FILE[class_name]
        # Handle paths with subdirectories (e.g., "modules/Module")
        if "/" in base_name:
            path = SRC_DIR / f"{base_name}.h"
            if path.exists():
                return path
        else:
            for ext in [".h", ".hpp"]:
                path = SRC_DIR / f"{base_name}{ext}"
                if path.exists():
                    return path
            # Try subdirectories
            for subdir in ["core", "modules", "gui", "utils", "data", "input", "shell"]:
                path = SRC_DIR / subdir / f"{base_name}{ext}"
                if path.exists():
                    return path
    
    # Standard search: try exact match first
    for ext in [".h", ".hpp"]:
        path = SRC_DIR / f"{class_name}{ext}"
        if path.exists():
            return path
    
    # Search in subdirectories
    for subdir in ["core", "modules", "gui", "utils", "data", "input", "shell"]:
        for ext in [".h", ".hpp"]:
            path = SRC_DIR / subdir / f"{class_name}{ext}"
            if path.exists():
                return path
    
    # Try with different naming conventions
    # Convert CamelCase to snake_case or lowercase
    variants = [
        class_name,
        class_name.lower(),
        class_name[0].lower() + class_name[1:] if class_name else "",
    ]
    
    for variant in variants:
        for subdir in ["core", "modules", "gui", "utils", "data", "input", "shell"]:
            for ext in [".h", ".hpp"]:
                path = SRC_DIR / subdir / f"{variant}{ext}"
                if path.exists():
                    return path
    
    return None


def find_cpp_file(class_name: str) -> Optional[Path]:
    """Find the cpp file for a class (if exists)"""
    # Check explicit mapping first
    if class_name in CLASS_TO_FILE:
        base_name = CLASS_TO_FILE[class_name]
        path = SRC_DIR / f"{base_name}.cpp"
        if path.exists():
            return path
        # Try subdirectories
        for subdir in ["core", "modules", "gui", "utils", "data", "input", "shell"]:
            path = SRC_DIR / subdir / f"{base_name}.cpp"
            if path.exists():
                return path
    
    # Standard search
    path = SRC_DIR / f"{class_name}.cpp"
    if path.exists():
        return path
    
    # Search in subdirectories
    for subdir in ["core", "modules", "gui", "utils", "data", "input", "shell"]:
        path = SRC_DIR / subdir / f"{class_name}.cpp"
        if path.exists():
            return path
    
    return None


def extract_struct_members(header_path: Path, struct_name: str) -> List[str]:
    """Extract public members/methods from a struct definition"""
    if not header_path.exists():
        return []
    
    try:
        content = header_path.read_text(encoding='utf-8')
    except Exception:
        return []
    
    # Find struct definition
    struct_pattern = rf'\bstruct\s+{re.escape(struct_name)}\b'
    struct_match = re.search(struct_pattern, content)
    if not struct_match:
        return []
    
    # Extract members (for structs, everything is public by default)
    methods = []
    start_pos = struct_match.end()
    # Find the matching closing brace
    brace_count = 1
    i = start_pos
    while i < len(content) and brace_count > 0:
        if content[i] == '{':
            brace_count += 1
        elif content[i] == '}':
            brace_count -= 1
        i += 1
    
    struct_body = content[start_pos:i-1]
    
    # Extract method-like declarations (functions, not just data members)
    # Look for patterns like: returnType methodName(params);
    method_pattern = r'(\w+)\s*\([^)]*\)\s*(?:const\s*)?(?:=\s*0\s*)?[;{]'
    for match in re.finditer(method_pattern, struct_body):
        method_name = match.group(1)
        if method_name and method_name not in ['~', 'operator'] and method_name not in EXCLUDE_METHODS:
            if method_name not in methods:
                methods.append(method_name)
    
    return methods


def extract_all_public_methods(header_path: Path) -> List[str]:
    """Extract ALL public methods from a C++ header file (no filtering)"""
    if not header_path.exists():
        return []
    
    try:
        content = header_path.read_text(encoding='utf-8')
    except Exception as e:
        print(f"  Warning: Could not read {header_path}: {e}")
        return []
    
    methods = []
    in_public_section = False
    in_class = False
    brace_depth = 0
    
    # Remove comments and strings to avoid false matches
    content_no_comments = re.sub(r'//.*?$', '', content, flags=re.MULTILINE)
    content_no_comments = re.sub(r'/\*.*?\*/', '', content_no_comments, flags=re.DOTALL)
    content_no_comments = re.sub(r'"[^"]*"', '""', content_no_comments)
    content_no_comments = re.sub(r"'[^']*'", "''", content_no_comments)
    
    lines = content_no_comments.split('\n')
    
    for i, line in enumerate(lines):
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
        
        # Extract method declarations in public section
        if in_public_section:
            # Match method declarations - more flexible pattern
            method_patterns = [
                r'(\w+(?:\s*::\s*\w+)*(?:\s*[*&<>,\s])*)\s+(\w+)\s*\([^)]*\)\s*(?:const\s*)?(?:=\s*0\s*)?;',
                r'(\w+(?:\s*::\s*\w+)*(?:\s*[*&<>,\s])*)\s+(\w+)\s*\([^)]*\)\s*(?:const\s*)?\s*\{',
                r'(std::\w+(?:\s*<\s*[^>]+\s*>)?(?:\s*[*&])?)\s+(\w+)\s*\([^)]*\)\s*(?:const\s*)?(?:=\s*0\s*)?;',
                r'(std::\w+(?:\s*<\s*[^>]+\s*>)?(?:\s*[*&])?)\s+(\w+)\s*\([^)]*\)\s*(?:const\s*)?\s*\{',
                r'(void)\s+(\w+)\s*\([^)]*\)\s*(?:const\s*)?(?:=\s*0\s*)?;',
                r'(void)\s+(\w+)\s*\([^)]*\)\s*(?:const\s*)?\s*\{',
            ]
            
            # Simplified pattern: match method name followed by (
            # This catches: bool connectAudio(...); void setRegistry(...) { ... }
            method_match = re.search(r'\b(\w+)\s*\([^)]*\)\s*(?:const\s*)?(?:=\s*0\s*)?[;{]', line)
            if method_match:
                method_name = method_match.group(1)
                # Skip constructors, destructors, operators
                if method_name and method_name not in ['~', 'operator'] and method_name not in EXCLUDE_METHODS:
                    # Get class name to skip constructors
                    class_name_match = None
                    for j in range(i, -1, -1):
                        class_match = re.search(r'\bclass\s+(\w+)', lines[j])
                        if class_match:
                            class_name_match = class_match.group(1)
                            break
                    
                    # Skip if it's a constructor (same as class name)
                    if class_name_match and method_name == class_name_match:
                        continue
                    
                    # Include all other methods
                    if method_name not in methods:  # Avoid duplicates
                        methods.append(method_name)
    
    return methods


def extract_public_methods(header_path: Path) -> List[str]:
    """Extract public methods from a C++ header file"""
    if not header_path.exists():
        return []
    
    try:
        content = header_path.read_text(encoding='utf-8')
    except Exception as e:
        print(f"  Warning: Could not read {header_path}: {e}")
        return []
    
    methods = []
    in_public_section = False
    in_class = False
    brace_depth = 0
    
    # Remove comments and strings to avoid false matches
    content_no_comments = re.sub(r'//.*?$', '', content, flags=re.MULTILINE)
    content_no_comments = re.sub(r'/\*.*?\*/', '', content_no_comments, flags=re.DOTALL)
    content_no_comments = re.sub(r'"[^"]*"', '""', content_no_comments)
    content_no_comments = re.sub(r"'[^']*'", "''", content_no_comments)
    
    lines = content_no_comments.split('\n')
    
    for i, line in enumerate(lines):
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
        
        # Extract method declarations in public section
        if in_public_section:
            # Match method declarations - more flexible pattern
            # Handles: return_type methodName(params) [const] [{] [= 0] [;]
            # Also handles inline methods with {}
            method_patterns = [
                # Standard method: type name(params);
                r'(\w+(?:\s*::\s*\w+)*(?:\s*[*&<>,\s])*)\s+(\w+)\s*\([^)]*\)\s*(?:const\s*)?(?:=\s*0\s*)?;',
                # Inline method: type name(params) { ... }
                r'(\w+(?:\s*::\s*\w+)*(?:\s*[*&<>,\s])*)\s+(\w+)\s*\([^)]*\)\s*(?:const\s*)?\s*\{',
                # Return type with template: std::vector<...> name(params);
                r'(std::\w+(?:\s*<\s*[^>]+\s*>)?(?:\s*[*&])?)\s+(\w+)\s*\([^)]*\)\s*(?:const\s*)?(?:=\s*0\s*)?;',
                r'(std::\w+(?:\s*<\s*[^>]+\s*>)?(?:\s*[*&])?)\s+(\w+)\s*\([^)]*\)\s*(?:const\s*)?\s*\{',
                # void name(params);
                r'(void)\s+(\w+)\s*\([^)]*\)\s*(?:const\s*)?(?:=\s*0\s*)?;',
                r'(void)\s+(\w+)\s*\([^)]*\)\s*(?:const\s*)?\s*\{',
            ]
            
            for pattern in method_patterns:
                method_match = re.search(pattern, line)
                if method_match:
                    method_name = method_match.group(2) if len(method_match.groups()) >= 2 else method_match.group(1)
                    # Skip constructors, destructors, operators
                    if method_name and method_name not in ['~', 'operator', 'ConnectionManager']:
                        # Filter by interaction keywords
                        if any(keyword in method_name.lower() for keyword in INTERACTION_KEYWORDS):
                            if method_name not in EXCLUDE_METHODS:
                                if method_name not in methods:  # Avoid duplicates
                                    methods.append(method_name)
                    break
    
    return methods


def get_key_interaction_methods(class_name: str, all_methods: List[str]) -> List[str]:
    """Select the most important interaction methods"""
    if not all_methods:
        return []
    
    # Priority: methods that clearly interact with other modules
    priority_methods = []
    medium_priority = []
    other_methods = []
    
    for method in all_methods:
        method_lower = method.lower()
        
        # High priority: connection, registration, execution methods
        if any(keyword in method_lower for keyword in ['connect', 'disconnect', 'register', 'execute', 'route', 'subscribe', 'unsubscribe']):
            priority_methods.append(method)
        # Medium priority: getters/setters that return module references
        elif method.startswith('get') and any(keyword in method_lower for keyword in ['module', 'manager', 'router', 'registry', 'factory', 'connection', 'state']):
            medium_priority.append(method)
        # Medium priority: subscription/notification methods
        elif any(keyword in method_lower for keyword in ['notify', 'on', 'set']):
            medium_priority.append(method)
        # Medium priority: load/save/serialize methods
        elif any(keyword in method_lower for keyword in ['load', 'save', 'serialize', 'deserialize']):
            medium_priority.append(method)
        else:
            other_methods.append(method)
    
    # Return up to 8 methods, prioritizing interaction methods
    result = priority_methods[:6]
    if len(result) < 8:
        result.extend(medium_priority[:8 - len(result)])
    if len(result) < 8:
        result.extend(other_methods[:8 - len(result)])
    
    # If still no methods, return first 8 methods (better than nothing)
    if not result and all_methods:
        result = all_methods[:8]
    
    return result[:8]  # Limit to 8 methods


def get_fallback_description(class_name: str) -> str:
    """Generate a fallback description based on class name patterns"""
    fallbacks = {
        "EngineState": "Immutable snapshot of engine state for rendering",
        "Engine": "Central headless core managing modules and execution",
        "ConnectionManager": "Unified connection management for audio/video/parameters",
        "ModuleRegistry": "Centralized storage and lookup for module instances",
        "ModuleFactory": "Factory for creating module instances",
        "ParameterRouter": "Routes parameter changes between modules",
        "SessionManager": "Manages saving and loading application sessions",
        "ProjectManager": "Manages project files and assets",
        "PatternRuntime": "Runtime system for pattern evaluation",
        "ScriptManager": "Generates and manages Lua scripts from state",
        "Clock": "Central timing system for audio/video synchronization",
        "CommandExecutor": "Executes commands with undo/redo support",
        "AudioRouter": "Routes audio signals between modules",
        "VideoRouter": "Routes video signals between modules",
        "EventRouter": "Manages event subscriptions between modules",
        "AssetLibrary": "Manages media assets and references",
        "MediaConverter": "Converts between media formats",
        "InputRouter": "Routes input events to modules",
        "ExpressionParser": "Parses mathematical expressions",
        # Structs and data types
        "TriggerEvent": "Event data for discrete step triggers with parameters",
        "Port": "Describes an input or output port on a module for routing",
        "SampleRef": "Contains shared audio data and video path for efficient memory usage",
        "Voice": "Represents an active playback instance in MultiSampler",
        "ofBaseApp": "Base class from openFrameworks framework (external dependency)",
    }
    return fallbacks.get(class_name, "")


def extract_class_description(header_path: Path, class_name: str) -> str:
    """Extract class description from header file comments"""
    if not header_path.exists():
        return get_fallback_description(class_name)
    
    try:
        content = header_path.read_text(encoding='utf-8')
    except Exception:
        return get_fallback_description(class_name)
    
    # Find the class or struct definition
    class_pattern = rf'\b(class|struct)\s+{re.escape(class_name)}\b'
    class_match = re.search(class_pattern, content)
    if not class_match:
        # For structs, also try without word boundary (might be in a comment or typedef)
        class_pattern = rf'(struct|class)\s+{re.escape(class_name)}\b'
        class_match = re.search(class_pattern, content)
        if not class_match:
            return get_fallback_description(class_name)
    
    # Look backwards from class definition for documentation comment
    start_pos = class_match.start()
    before_class = content[:start_pos]
    
    # Find the last /** ... */ comment block before the class
    # Match /** ... */ blocks (multiline)
    doc_blocks = list(re.finditer(r'/\*\*\s*(.*?)\s*\*/', before_class, re.DOTALL))
    
    if doc_blocks:
        # Get the last comment block before the class
        last_block = doc_blocks[-1]
        doc_text = last_block.group(1)
        
        # Extract first meaningful line (usually the class description)
        lines = [line.strip() for line in doc_text.split('\n') if line.strip()]
        
        if lines:
            # First line is usually the class name and brief description
            first_line = lines[0]
            # Remove class name if it's there (e.g., "Engine - The central headless core")
            if ' - ' in first_line:
                desc = first_line.split(' - ', 1)[1]
            elif first_line.startswith(class_name):
                desc = first_line[len(class_name):].strip(' -')
            else:
                desc = first_line
            
            # Clean up: remove asterisks, extra spaces
            desc = re.sub(r'\*\s*', '', desc)
            desc = re.sub(r'\s+', ' ', desc).strip()
            
            # Limit length
            if len(desc) > 120:
                desc = desc[:117] + "..."
            
            if desc:
                return desc
    
    # Fallback to pattern-based description
    return get_fallback_description(class_name)


def format_node_text(class_name: str, methods: List[str], has_cpp: bool, description: str = "") -> str:
    """Format node text with title, description, and methods"""
    # Title with file extensions
    if has_cpp:
        title = f"# {class_name}.cpp/h"
    else:
        title = f"# {class_name}.h"
    
    # Build content
    lines = [title, ""]
    
    # Add description (especially important when no methods found)
    if description:
        lines.append(f"*{description}*")
        lines.append("")
    elif not methods:
        # If no description and no methods, add a generic note
        lines.append("*(Class definition found)*")
        lines.append("")
    
    if methods:
        lines.append("**Key Methods:**")
        for method in methods:
            lines.append(f"- `{method}()`")
    elif not description:
        # Only show this if we also don't have a description
        lines.append("*(No public methods found)*")
    
    return "\n".join(lines)


def main():
    """Update canvas nodes with proper titles and methods"""
    print(f"Reading canvas: {CANVAS_PATH}")
    
    # Read existing canvas
    with open(CANVAS_PATH, 'r', encoding='utf-8') as f:
        canvas_data = json.load(f)
    
    nodes = canvas_data.get("nodes", [])
    print(f"Found {len(nodes)} nodes")
    
    updated_count = 0
    not_found_count = 0
    
    for node in nodes:
        if node.get("type") != "text":
            continue
        
        # Extract class name from existing text
        text = node.get("text", "").strip()
        
        # Try to extract class name (remove markdown, abstract markers, etc.)
        class_name = None
        
        # Pattern 1: "# ClassName.cpp/h" or "# ClassName.h"
        match = re.search(r'#\s*(\w+)\.(?:cpp/)?h', text)
        if match:
            class_name = match.group(1)
        else:
            # Pattern 2: "*ClassName*" (abstract)
            match = re.search(r'\*(\w+)\*', text)
            if match:
                class_name = match.group(1)
            else:
                # Pattern 3: Just the class name
                lines = text.split('\n')
                first_line = lines[0].strip()
                # Remove common prefixes/suffixes
                class_name = first_line.replace(" (abstract)", "").replace(".h", "").replace(".cpp/h", "").replace(".cpp", "").strip()
        
        if not class_name:
            print(f"  Warning: Could not extract class name from node: {text[:50]}...")
            continue
        
        # Find header file
        header_path = find_class_file(class_name)
        cpp_path = find_cpp_file(class_name)
        has_cpp = cpp_path is not None
        
        if not header_path:
            print(f"  ⚠️  {class_name}: Header file not found, using fallback description")
            not_found_count += 1
            # Use fallback description
            description = get_fallback_description(class_name)
            # Try to find methods from any related files
            all_methods = []
            key_methods = []
            # Still update title with description
            new_text = format_node_text(class_name, key_methods, False, description)
            node["text"] = new_text
            updated_count += 1
            continue
        
        # Extract methods - get all public methods first, then filter
        print(f"  Processing {class_name}...")
        
        # Check if it's a struct (structs are in Module.h, MultiSampler.h, etc.)
        is_struct = class_name in ["TriggerEvent", "Port", "SampleRef", "Voice"]
        
        if is_struct:
            all_methods = extract_struct_members(header_path, class_name)
        else:
            all_methods = extract_all_public_methods(header_path)
        
        key_methods = get_key_interaction_methods(class_name, all_methods)
        
        # Extract class description
        description = extract_class_description(header_path, class_name)
        
        # Format new text
        new_text = format_node_text(class_name, key_methods, has_cpp, description)
        node["text"] = new_text
        updated_count += 1
        
        if key_methods:
            print(f"    ✓ Found {len(all_methods)} methods, selected {len(key_methods)} key methods")
        elif description:
            print(f"    ✓ Added description: {description[:50]}...")
        else:
            print(f"    ⚠️  No methods or description found")
    
    # Write updated canvas
    with open(CANVAS_PATH, 'w', encoding='utf-8') as f:
        json.dump(canvas_data, f, indent="\t", ensure_ascii=False)
    
    print(f"\n✅ Canvas updated successfully!")
    print(f"   Updated {updated_count} nodes")
    print(f"   Files not found: {not_found_count}")


if __name__ == "__main__":
    main()

