#!/usr/bin/env bash
SHADER_PATH="${1:-shaders/gradient.spv}"
SDL_IM_MODULE=none exec ./build/Debug/Compute "$SHADER_PATH"
