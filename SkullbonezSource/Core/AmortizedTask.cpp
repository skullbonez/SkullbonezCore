/*
File: SkullbonezSource/Core/AmortizedTask.cpp
Purpose:
  Provides the project compilation anchor for the header-only task template.

Mental model:
  AmortizedTask is templated so its worker operation remains a typed value. The
  implementation therefore lives in the header and this translation unit keeps
  the existing project layout stable.

Glossary:
  Worker pool: Persistent thread group that runs bounded jobs outside the main
  thread.
  Atomic cursor: Thread-safe index used to claim each slice once.
  Budget: Maximum item count processed by one submitted chunk.

Invariants:
  - At most one worker chunk is in flight per task; SubmitTick is a no-op while
    the previous chunk still owns the cursor.
  - The cursor only advances by claimed budgets, and completion is published
    after the last claimed range reaches totalItems.

Related:
  - SkullbonezSource/Core/AmortizedTask.h
  - SkullbonezSource/Core/WorkerPool.h
*/

#include "AmortizedTask.h"
