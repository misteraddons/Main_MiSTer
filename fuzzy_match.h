#ifndef FUZZY_MATCH_H
#define FUZZY_MATCH_H

#include <stdbool.h>
#include <stddef.h>

// Fuzzy matching utilities for game title comparison

// Calculate Levenshtein distance between two strings
int levenshtein_distance(const char* s1, const char* s2);

// Normalize a game title for fuzzy matching
// Removes common words, normalizes punctuation, handles numbers
void normalize_game_title(const char* input, char* output, size_t output_size);

// Fuzzy match two game titles
// Returns a score from 0-100, where 100 is perfect match
int fuzzy_match_score(const char* title1, const char* title2);

// Check if title matches with fuzzy logic
// threshold: minimum score to consider a match (0-100)
bool fuzzy_match(const char* title1, const char* title2, int threshold);

// Extract base game name without region/version info
void extract_base_name(const char* title, char* base_name, size_t size);

// Compare regions and return priority score
// Higher score = better match for user's preferred region
int region_priority_score(const char* region, const char* preferred_region);

#endif // FUZZY_MATCH_H