#pragma once

#include <array>

struct PromptTemplate {
    unsigned id;
    const char* text;
};

// TLK entry IDs and controller-action placeholders, expanded using current bindings.
extern const std::array<PromptTemplate, 102> prompt_templates;
