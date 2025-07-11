#include "fuzzy_match.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

// Common words to remove for fuzzy matching
static const char* common_words[] = {
    "the", "a", "an", "and", "or", "of", "in", "on", "at", "to", "for",
    "special", "edition", "enhanced", "deluxe", "ultimate", "complete",
    "director", "cut", "version", "remastered", "remake", "redux",
    NULL
};

// Number conversions
static const struct {
    const char* text;
    const char* number;
} number_conversions[] = {
    {"zero", "0"}, {"one", "1"}, {"two", "2"}, {"three", "3"}, {"four", "4"},
    {"five", "5"}, {"six", "6"}, {"seven", "7"}, {"eight", "8"}, {"nine", "9"},
    {"ii", "2"}, {"iii", "3"}, {"iv", "4"}, {"v", "5"}, {"vi", "6"},
    {"vii", "7"}, {"viii", "8"}, {"ix", "9"}, {"x", "10"},
    {NULL, NULL}
};

// Calculate Levenshtein distance
int levenshtein_distance(const char* s1, const char* s2) {
    int len1 = strlen(s1);
    int len2 = strlen(s2);
    
    // Create a 2D array for dynamic programming
    int** dist = (int**)malloc((len1 + 1) * sizeof(int*));
    for (int i = 0; i <= len1; i++) {
        dist[i] = (int*)malloc((len2 + 1) * sizeof(int));
    }
    
    // Initialize base cases
    for (int i = 0; i <= len1; i++) dist[i][0] = i;
    for (int j = 0; j <= len2; j++) dist[0][j] = j;
    
    // Fill the matrix
    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (tolower(s1[i-1]) != tolower(s2[j-1])) ? 1 : 0;
            int del = dist[i-1][j] + 1;
            int ins = dist[i][j-1] + 1;
            int sub = dist[i-1][j-1] + cost;
            
            dist[i][j] = del < ins ? del : ins;
            if (sub < dist[i][j]) dist[i][j] = sub;
        }
    }
    
    int result = dist[len1][len2];
    
    // Free memory
    for (int i = 0; i <= len1; i++) {
        free(dist[i]);
    }
    free(dist);
    
    return result;
}

// Check if a word is in the common words list
static bool is_common_word(const char* word) {
    for (int i = 0; common_words[i]; i++) {
        if (strcasecmp(word, common_words[i]) == 0) {
            return true;
        }
    }
    return false;
}

// Convert text numbers to digits
static void convert_numbers(char* text) {
    for (int i = 0; number_conversions[i].text; i++) {
        char* pos = text;
        while ((pos = strstr(pos, number_conversions[i].text)) != NULL) {
            int text_len = strlen(number_conversions[i].text);
            int num_len = strlen(number_conversions[i].number);
            
            // Check if it's a whole word
            bool is_word = (pos == text || !isalnum(*(pos-1))) &&
                          (*(pos + text_len) == '\0' || !isalnum(*(pos + text_len)));
            
            if (is_word) {
                memmove(pos + num_len, pos + text_len, strlen(pos + text_len) + 1);
                memcpy(pos, number_conversions[i].number, num_len);
                pos += num_len;
            } else {
                pos++;
            }
        }
    }
}

// Normalize a game title
void normalize_game_title(const char* input, char* output, size_t output_size) {
    char temp[1024];
    char* write_ptr = temp;
    const char* read_ptr = input;
    
    // Convert to lowercase and remove special characters
    while (*read_ptr && (write_ptr - temp) < (int)(sizeof(temp) - 1)) {
        if (isalnum(*read_ptr) || *read_ptr == ' ') {
            *write_ptr++ = tolower(*read_ptr);
        } else {
            // Replace special chars with space
            if (write_ptr > temp && *(write_ptr-1) != ' ') {
                *write_ptr++ = ' ';
            }
        }
        read_ptr++;
    }
    *write_ptr = '\0';
    
    // Convert numbers
    convert_numbers(temp);
    
    // Remove common words
    char final[1024];
    char* final_ptr = final;
    char* word = strtok(temp, " ");
    
    while (word) {
        if (!is_common_word(word)) {
            if (final_ptr > final) {
                *final_ptr++ = ' ';
            }
            strcpy(final_ptr, word);
            final_ptr += strlen(word);
        }
        word = strtok(NULL, " ");
    }
    *final_ptr = '\0';
    
    // Copy to output
    strncpy(output, final, output_size - 1);
    output[output_size - 1] = '\0';
}

// Calculate fuzzy match score
int fuzzy_match_score(const char* title1, const char* title2) {
    char norm1[1024], norm2[1024];
    
    // Normalize both titles
    normalize_game_title(title1, norm1, sizeof(norm1));
    normalize_game_title(title2, norm2, sizeof(norm2));
    
    // If normalized titles are identical, perfect match
    if (strcmp(norm1, norm2) == 0) {
        return 100;
    }
    
    // Hierarchical scoring system:
    // 1. First words (base title) - 60%
    // 2. Series number match - 25% 
    // 3. Overall similarity - 15%
    
    int base_score = 0;
    int series_score = 0;
    int similarity_score = 0;
    
    // Extract base titles (everything before first number or parenthesis)
    char base1[256], base2[256];
    extract_base_game_name(norm1, base1, sizeof(base1));
    extract_base_game_name(norm2, base2, sizeof(base2));
    
    // Score base title match (highest priority)
    if (strcmp(base1, base2) == 0) {
        base_score = 100; // Perfect base match
    } else {
        // Penalize heavily if base titles don't match
        int base_distance = levenshtein_distance(base1, base2);
        int base_max_len = strlen(base1) > strlen(base2) ? strlen(base1) : strlen(base2);
        if (base_max_len > 0) {
            base_score = 100 - (base_distance * 100 / base_max_len);
            if (base_score < 0) base_score = 0;
            // Additional penalty for different base titles
            if (base_score < 90) base_score = base_score / 2;
        }
    }
    
    // Score series number match (second priority)
    char series1[10], series2[10];
    extract_series_number(norm1, series1, sizeof(series1));
    extract_series_number(norm2, series2, sizeof(series2));
    
    if (strlen(series1) == 0 && strlen(series2) == 0) {
        series_score = 100; // Both have no series number
    } else if (strcmp(series1, series2) == 0) {
        series_score = 100; // Same series number
    } else {
        series_score = 0; // Different series numbers = major penalty
    }
    
    // Overall similarity score (lowest priority)
    int distance = levenshtein_distance(norm1, norm2);
    int max_len = strlen(norm1) > strlen(norm2) ? strlen(norm1) : strlen(norm2);
    if (max_len > 0) {
        similarity_score = 100 - (distance * 100 / max_len);
        if (similarity_score < 0) similarity_score = 0;
    }
    
    // Weighted combination: 60% base + 25% series + 15% similarity
    int final_score = (base_score * 60 + series_score * 25 + similarity_score * 15) / 100;
    
    return final_score;
}

// Extract base game name (everything before numbers or series indicators)
void extract_base_game_name(const char* title, char* base_name, size_t size) {
    const char* src = title;
    char* dst = base_name;
    size_t remaining = size - 1;
    
    while (*src && remaining > 0) {
        // Stop at first digit or common series indicators
        if (isdigit(*src)) break;
        if (strncmp(src, " ii", 3) == 0 || strncmp(src, " iii", 4) == 0 || 
            strncmp(src, " iv", 3) == 0 || strncmp(src, " v", 2) == 0) break;
        
        *dst++ = *src++;
        remaining--;
    }
    
    // Trim trailing spaces
    while (dst > base_name && *(dst-1) == ' ') dst--;
    *dst = '\0';
}

// Extract series number from title
void extract_series_number(const char* title, char* series, size_t size) {
    series[0] = '\0';
    
    const char* p = title;
    while (*p) {
        // Look for standalone digits
        if (isdigit(*p) && (p == title || *(p-1) == ' ')) {
            if (p[1] == '\0' || p[1] == ' ' || p[1] == '(' || p[1] == '[') {
                snprintf(series, size, "%c", *p);
                return;
            }
        }
        
        // Look for Roman numerals
        if (strncmp(p, " ii", 3) == 0) { strcpy(series, "2"); return; }
        if (strncmp(p, " iii", 4) == 0) { strcpy(series, "3"); return; }
        if (strncmp(p, " iv", 3) == 0) { strcpy(series, "4"); return; }
        if (strncmp(p, " v", 2) == 0 && (p[2] == '\0' || p[2] == ' ')) { strcpy(series, "5"); return; }
        if (strncmp(p, " vi", 3) == 0) { strcpy(series, "6"); return; }
        if (strncmp(p, " vii", 4) == 0) { strcpy(series, "7"); return; }
        if (strncmp(p, " viii", 5) == 0) { strcpy(series, "8"); return; }
        if (strncmp(p, " ix", 3) == 0) { strcpy(series, "9"); return; }
        if (strncmp(p, " x", 2) == 0 && (p[2] == '\0' || p[2] == ' ')) { strcpy(series, "10"); return; }
        
        p++;
    }
}

// Check if titles match with threshold
bool fuzzy_match(const char* title1, const char* title2, int threshold) {
    return fuzzy_match_score(title1, title2) >= threshold;
}

// Extract base game name without region/version
void extract_base_name(const char* title, char* base_name, size_t size) {
    const char* end = title + strlen(title); // Default to end of string
    
    // Find the start of region/version info (usually in parentheses)
    const char* paren = strchr(title, '(');
    if (paren) {
        end = paren;
    }
    
    // Also check for common version markers before parentheses
    const char* markers[] = {" - Rev ", " - v", " - V", " [", NULL};
    for (int i = 0; markers[i]; i++) {
        const char* marker = strstr(title, markers[i]);
        if (marker && (!paren || marker < paren)) {
            end = marker;
        }
    }
    
    // Copy base name
    int len = end - title;
    printf("DEBUG: extract_base_name input='%s', end-title=%d, size=%zu\n", title, len, size);
    if (len >= (int)size) len = size - 1;
    
    strncpy(base_name, title, len);
    base_name[len] = '\0';
    printf("DEBUG: extract_base_name output='%s', final_len=%d\n", base_name, len);
    
    // Trim trailing spaces
    while (len > 0 && base_name[len-1] == ' ') {
        base_name[--len] = '\0';
    }
}

// Region priority scoring
int region_priority_score(const char* region, const char* preferred_region) {
    // Direct match with preferred region
    if (strcasecmp(region, preferred_region) == 0) {
        return 100;
    }
    
    // Default priority order: USA -> Europe -> Japan -> World -> Others
    struct {
        const char* region;
        int priority;
    } default_priorities[] = {
        {"USA", 90},
        {"US", 90},
        {"NTSC-U", 90},
        {"Europe", 80},
        {"EUR", 80},
        {"PAL", 80},
        {"Japan", 70},
        {"JPN", 70},
        {"JP", 70},
        {"NTSC-J", 70},
        {"World", 60},
        {"Asia", 50},
        {NULL, 0}
    };
    
    // Check against default priorities
    for (int i = 0; default_priorities[i].region; i++) {
        if (strcasecmp(region, default_priorities[i].region) == 0) {
            return default_priorities[i].priority;
        }
    }
    
    // Unknown region
    return 10;
}