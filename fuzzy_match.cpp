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
    
    // Calculate Levenshtein distance
    int distance = levenshtein_distance(norm1, norm2);
    int max_len = strlen(norm1) > strlen(norm2) ? strlen(norm1) : strlen(norm2);
    
    if (max_len == 0) return 0;
    
    // Convert distance to score (0-100)
    int score = 100 - (distance * 100 / max_len);
    if (score < 0) score = 0;
    
    return score;
}

// Check if titles match with threshold
bool fuzzy_match(const char* title1, const char* title2, int threshold) {
    return fuzzy_match_score(title1, title2) >= threshold;
}

// Extract base game name without region/version
void extract_base_name(const char* title, char* base_name, size_t size) {
    const char* end = title;
    
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
    if (len >= (int)size) len = size - 1;
    
    strncpy(base_name, title, len);
    base_name[len] = '\0';
    
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