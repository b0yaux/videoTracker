#!/bin/bash

# VideoTracker Performance Monitor
# Real-time performance analysis and bottleneck identification
# Usage: ./monitor_performance.sh [log_file]

# Default log file or use provided argument
LOG_FILE=${1:-"console.log"}
SCRIPT_DIR=$(dirname "$0")

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Performance thresholds
EXCELLENT_THRESHOLD=10
GOOD_THRESHOLD=15
POOR_THRESHOLD=25

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}    VideoTracker Performance Monitor    ${NC}"
echo -e "${CYAN}========================================${NC}"
echo ""
echo -e "${BLUE}Monitoring log file: ${LOG_FILE}${NC}"
echo -e "${BLUE}Performance thresholds:${NC}"
echo -e "  ${GREEN}Excellent: < ${EXCELLENT_THRESHOLD}ms${NC}"
echo -e "  ${YELLOW}Good: ${EXCELLENT_THRESHOLD}-${GOOD_THRESHOLD}ms${NC}"
echo -e "  ${RED}Poor: > ${POOR_THRESHOLD}ms${NC}"
echo ""

# Function to colorize performance values
colorize_perf() {
    local value=$1
    local threshold_type=${2:-"time"}
    
    if [[ $threshold_type == "time" ]]; then
        if (( $(echo "$value < $EXCELLENT_THRESHOLD" | bc -l) )); then
            echo -e "${GREEN}${value}ms${NC}"
        elif (( $(echo "$value < $GOOD_THRESHOLD" | bc -l) )); then
            echo -e "${YELLOW}${value}ms${NC}"
        elif (( $(echo "$value < $POOR_THRESHOLD" | bc -l) )); then
            echo -e "${YELLOW}${value}ms${NC}"
        else
            echo -e "${RED}${value}ms${NC}"
        fi
    elif [[ $threshold_type == "cache" ]]; then
        if (( $(echo "$value > 80" | bc -l) )); then
            echo -e "${GREEN}${value}%${NC}"
        elif (( $(echo "$value > 60" | bc -l) )); then
            echo -e "${YELLOW}${value}%${NC}"
        else
            echo -e "${RED}${value}%${NC}"
        fi
    fi
}

# Function to analyze performance trends
analyze_trends() {
    local component=$1
    echo -e "\n${PURPLE}=== Performance Analysis: $component ===${NC}"
    
    # Get recent performance data
    local recent_times=$(tail -100 "$LOG_FILE" | grep "$component.*drawWaveform" | grep "\[PERF\]" | \
                        sed -E 's/.*drawWaveform: ([0-9.]+)ms.*/\1/' | tail -10)
    
    if [[ -n "$recent_times" ]]; then
        local count=0
        local total=0
        local max=0
        local min=999999
        
        while IFS= read -r time; do
            if [[ -n "$time" ]]; then
                count=$((count + 1))
                total=$(echo "$total + $time" | bc -l)
                max=$(echo "if ($time > $max) $time else $max" | bc -l)
                min=$(echo "if ($time < $min) $time else $min" | bc -l)
            fi
        done <<< "$recent_times"
        
        if [[ $count -gt 0 ]]; then
            local avg=$(echo "scale=2; $total / $count" | bc -l)
            echo -e "  Recent performance (last $count samples):"
            echo -e "    Average: $(colorize_perf $avg)"
            echo -e "    Range: $(colorize_perf $min) - $(colorize_perf $max)"
            
            # Performance status
            if (( $(echo "$avg < $EXCELLENT_THRESHOLD" | bc -l) )); then
                echo -e "    Status: ${GREEN}EXCELLENT${NC}"
            elif (( $(echo "$avg < $GOOD_THRESHOLD" | bc -l) )); then
                echo -e "    Status: ${YELLOW}GOOD${NC}"
            else
                echo -e "    Status: ${RED}NEEDS OPTIMIZATION${NC}"
            fi
        fi
    else
        echo -e "  ${YELLOW}No recent waveform performance data${NC}"
    fi
    
    # Check for cache effectiveness
    local cache_hits=$(tail -100 "$LOG_FILE" | grep "$component.*cache hit" | wc -l | xargs)
    local cache_misses=$(tail -100 "$LOG_FILE" | grep "$component.*cache miss" | wc -l | xargs)
    local cache_total=$((cache_hits + cache_misses))
    
    if [[ $cache_total -gt 0 ]]; then
        local cache_hit_rate=$(echo "scale=1; ($cache_hits * 100) / $cache_total" | bc -l)
        echo -e "  Cache hit rate: $(colorize_perf $cache_hit_rate cache) ($cache_hits/$cache_total)"
    fi
    
    # Check for budget warnings
    local budget_warnings=$(tail -100 "$LOG_FILE" | grep "$component.*BUDGET.*exceeded" | wc -l | xargs)
    if [[ $budget_warnings -gt 0 ]]; then
        echo -e "  ${RED}⚠ Budget exceeded $budget_warnings times recently${NC}"
    fi
}

# Function to show live performance stream
live_monitor() {
    echo -e "${CYAN}=== Live Performance Monitor (Press Ctrl+C to stop) ===${NC}"
    echo ""
    
    # Follow log file and filter performance-related entries
    tail -f "$LOG_FILE" 2>/dev/null | while read -r line; do
        timestamp=$(echo "$line" | grep -o '\[[0-9:\.]*\]' | head -1)
        
        # Waveform performance
        if echo "$line" | grep -q "\[PERF\].*drawWaveform"; then
            component=$(echo "$line" | sed -E 's/.*MediaPoolGUI: \[PERF\] '\''([^'\'']*)'\''.*drawWaveform: ([0-9.]+)ms.*/\1 \2/')
            name=$(echo $component | cut -d' ' -f1)
            time=$(echo $component | cut -d' ' -f2)
            echo -e "${timestamp} ${BLUE}WAVEFORM${NC} ${name}: $(colorize_perf $time)"
            
        # MediaList performance (new bottleneck)
        elif echo "$line" | grep -q "\[PERF\].*drawMediaList"; then
            component=$(echo "$line" | sed -E 's/.*MediaPoolGUI: \[PERF\] '\''([^'\'']*)'\''.*drawMediaList: ([0-9.]+)ms.*/\1 \2/')
            name=$(echo $component | cut -d' ' -f1)
            time=$(echo $component | cut -d' ' -f2)
            echo -e "${timestamp} ${PURPLE}MEDIALIST${NC} ${name}: $(colorize_perf $time)"
            
        # Cache events
        elif echo "$line" | grep -q "\[CACHE\].*cache hit"; then
            component=$(echo "$line" | sed -E 's/.*MediaPoolGUI: \[CACHE\] '\''([^'\'']*)'\''.*cache hit.*/\1/')
            echo -e "${timestamp} ${GREEN}CACHE HIT${NC} ${component}"
            
        elif echo "$line" | grep -q "\[CACHE\].*cache miss"; then
            component=$(echo "$line" | sed -E 's/.*MediaPoolGUI: \[CACHE\] '\''([^'\'']*)'\''.*cache miss.*/\1/')
            reasons=$(echo "$line" | sed -E 's/.*miss reasons: (.*)/\1/')
            echo -e "${timestamp} ${YELLOW}CACHE MISS${NC} ${component}: ${reasons}"
            
        # Budget warnings
        elif echo "$line" | grep -q "\[BUDGET\].*exceeded"; then
            component=$(echo "$line" | sed -E 's/.*MediaPoolGUI: \[BUDGET\] '\''([^'\'']*)'\''.*exceeded.*/\1/')
            echo -e "${timestamp} ${RED}BUDGET EXCEEDED${NC} ${component}"
            
        # Micro-benchmarks
        elif echo "$line" | grep -q "\[MICRO\].*TOTAL calc"; then
            if echo "$line" | grep -q "BUDGET_EXCEEDED"; then
                component=$(echo "$line" | sed -E 's/.*MediaPoolGUI: \[MICRO\] '\''([^'\'']*)'\''.*TOTAL calc: ([0-9.]+)ms.*/\1 \2/')
                name=$(echo $component | cut -d' ' -f1)
                time=$(echo $component | cut -d' ' -f2)
                echo -e "${timestamp} ${RED}CALC (TRUNCATED)${NC} ${name}: ${time}ms"
            fi
            
        # Slow frame warnings
        elif echo "$line" | grep -q "Slow frame detected"; then
            times=$(echo "$line" | grep -o '[0-9.]*ms' | head -3)
            echo -e "${timestamp} ${RED}SLOW FRAME${NC} Total: $(echo $times | cut -d' ' -f1) GUI: $(echo $times | cut -d' ' -f3)"
        fi
    done
}

# Main menu