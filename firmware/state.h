#pragma once
#include <Arduino.h>

enum MascotState : uint8_t {
  STATE_BOOT    = 0,
  STATE_IDLE    = 1,
  STATE_THINKING= 2,
  STATE_WORKING = 3,
  STATE_WAITING = 4,
  STATE_ERROR   = 5,
  STATE_DONE    = 6,
};

enum ToolIcon : uint8_t {
  TOOL_NONE  = 0,
  TOOL_READ  = 1,
  TOOL_EDIT  = 2,
  TOOL_WRITE = 3,
  TOOL_BASH  = 4,
  TOOL_GREP  = 5,
  TOOL_WEB   = 6,
  TOOL_TASK  = 7,
  TOOL_OTHER = 99,
};

struct AppState {
  MascotState state       = STATE_BOOT;
  ToolIcon    tool        = TOOL_NONE;
  char        message[64] = "Booting...";
  char        client[24]  = "claude-code";
  uint32_t    tokensUsed  = 0;
  uint32_t    tokensMax   = 0;
  uint32_t    lastUpdateMs= 0;
};

inline const char* stateName(MascotState s) {
  switch (s) {
    case STATE_BOOT:     return "Boot";
    case STATE_IDLE:     return "Idle";
    case STATE_THINKING: return "Thinking";
    case STATE_WORKING:  return "Working";
    case STATE_WAITING:  return "Waiting";
    case STATE_ERROR:    return "Error";
    case STATE_DONE:     return "Done";
  }
  return "?";
}

inline const char* toolName(ToolIcon t) {
  switch (t) {
    case TOOL_NONE:  return "";
    case TOOL_READ:  return "Read";
    case TOOL_EDIT:  return "Edit";
    case TOOL_WRITE: return "Write";
    case TOOL_BASH:  return "Bash";
    case TOOL_GREP:  return "Grep";
    case TOOL_WEB:   return "Web";
    case TOOL_TASK:  return "Task";
    case TOOL_OTHER: return "Tool";
  }
  return "";
}

inline ToolIcon parseToolName(const char* s) {
  if (!s || !*s) return TOOL_NONE;
  if (!strcasecmp(s, "Read"))         return TOOL_READ;
  if (!strcasecmp(s, "Edit"))         return TOOL_EDIT;
  if (!strcasecmp(s, "MultiEdit"))    return TOOL_EDIT;
  if (!strcasecmp(s, "Write"))        return TOOL_WRITE;
  if (!strcasecmp(s, "Bash"))         return TOOL_BASH;
  if (!strcasecmp(s, "Grep"))         return TOOL_GREP;
  if (!strcasecmp(s, "Glob"))         return TOOL_GREP;
  if (!strcasecmp(s, "WebFetch"))     return TOOL_WEB;
  if (!strcasecmp(s, "WebSearch"))    return TOOL_WEB;
  if (!strcasecmp(s, "Task"))         return TOOL_TASK;
  if (!strcasecmp(s, "Agent"))        return TOOL_TASK;
  return TOOL_OTHER;
}

inline MascotState parseStateName(const char* s) {
  if (!s || !*s) return STATE_IDLE;
  if (!strcasecmp(s, "idle"))     return STATE_IDLE;
  if (!strcasecmp(s, "thinking")) return STATE_THINKING;
  if (!strcasecmp(s, "working"))  return STATE_WORKING;
  if (!strcasecmp(s, "waiting"))  return STATE_WAITING;
  if (!strcasecmp(s, "error"))    return STATE_ERROR;
  if (!strcasecmp(s, "done"))     return STATE_DONE;
  return STATE_IDLE;
}
