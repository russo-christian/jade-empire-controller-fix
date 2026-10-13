#pragma once

void __fastcall prompt_token(void* talk, void*, void* token, void* output);
unsigned __fastcall prompt_text(void* talk, void*, unsigned id, void* output, unsigned expand,
                                unsigned variant);
