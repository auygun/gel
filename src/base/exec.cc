// Copyright (C) 2026 Opera Norway AS. All rights reserved.
//
// This file is an original work developed by Opera.

#include "base/exec.h"

void Exec::CloseStdin() {
  in_pipe_.Close(Pipe::kWrite);
}
