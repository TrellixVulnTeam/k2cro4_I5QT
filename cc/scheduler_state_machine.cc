// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/scheduler_state_machine.h"

#include "base/logging.h"
#include "base/stringprintf.h"

namespace cc {

SchedulerStateMachine::SchedulerStateMachine()
    : m_commitState(COMMIT_STATE_IDLE)
    , m_currentFrameNumber(0)
    , m_lastFrameNumberWhereDrawWasCalled(-1)
    , m_consecutiveFailedDraws(0)
    , m_maximumNumberOfFailedDrawsBeforeDrawIsForced(3)
    , m_needsRedraw(false)
    , m_needsForcedRedraw(false)
    , m_needsForcedRedrawAfterNextCommit(false)
    , m_needsCommit(false)
    , m_needsForcedCommit(false)
    , m_expectImmediateBeginFrame(false)
    , m_mainThreadNeedsLayerTextures(false)
    , m_insideVSync(false)
    , m_visible(false)
    , m_canBeginFrame(false)
    , m_canDraw(false)
    , m_drawIfPossibleFailed(false)
    , m_textureState(LAYER_TEXTURE_STATE_UNLOCKED)
    , m_contextState(CONTEXT_ACTIVE)
{
}

std::string SchedulerStateMachine::toString()
{
    std::string str;
    base::StringAppendF(&str, "m_commitState = %d; ", m_commitState);
    base::StringAppendF(&str, "m_currentFrameNumber = %d; ", m_currentFrameNumber);
    base::StringAppendF(&str, "m_lastFrameNumberWhereDrawWasCalled = %d; ", m_lastFrameNumberWhereDrawWasCalled);
    base::StringAppendF(&str, "m_consecutiveFailedDraws = %d; ", m_consecutiveFailedDraws);
    base::StringAppendF(&str, "m_maximumNumberOfFailedDrawsBeforeDrawIsForced = %d; ", m_maximumNumberOfFailedDrawsBeforeDrawIsForced);
    base::StringAppendF(&str, "m_needsRedraw = %d; ", m_needsRedraw);
    base::StringAppendF(&str, "m_needsForcedRedraw = %d; ", m_needsForcedRedraw);
    base::StringAppendF(&str, "m_needsForcedRedrawAfterNextCommit = %d; ", m_needsForcedRedrawAfterNextCommit);
    base::StringAppendF(&str, "m_needsCommit = %d; ", m_needsCommit);
    base::StringAppendF(&str, "m_needsForcedCommit = %d; ", m_needsForcedCommit);
    base::StringAppendF(&str, "m_expectImmediateBeginFrame = %d; ", m_expectImmediateBeginFrame);
    base::StringAppendF(&str, "m_mainThreadNeedsLayerTextures = %d; ", m_mainThreadNeedsLayerTextures);
    base::StringAppendF(&str, "m_insideVSync = %d; ", m_insideVSync);
    base::StringAppendF(&str, "m_visible = %d; ", m_visible);
    base::StringAppendF(&str, "m_canBeginFrame = %d; ", m_canBeginFrame);
    base::StringAppendF(&str, "m_canDraw = %d; ", m_canDraw);
    base::StringAppendF(&str, "m_drawIfPossibleFailed = %d; ", m_drawIfPossibleFailed);
    base::StringAppendF(&str, "m_textureState = %d; ", m_textureState);
    base::StringAppendF(&str, "m_contextState = %d; ", m_contextState);
    return str;
}

bool SchedulerStateMachine::hasDrawnThisFrame() const
{
    return m_currentFrameNumber == m_lastFrameNumberWhereDrawWasCalled;
}

bool SchedulerStateMachine::drawSuspendedUntilCommit() const
{
    if (!m_canDraw)
        return true;
    if (!m_visible)
        return true;
    if (m_textureState == LAYER_TEXTURE_STATE_ACQUIRED_BY_MAIN_THREAD)
        return true;
    return false;
}

bool SchedulerStateMachine::scheduledToDraw() const
{
    if (!m_needsRedraw)
        return false;
    if (drawSuspendedUntilCommit())
        return false;
    return true;
}

bool SchedulerStateMachine::shouldDraw() const
{
    if (m_needsForcedRedraw)
        return true;

    if (!scheduledToDraw())
        return false;
    if (!m_insideVSync)
        return false;
    if (hasDrawnThisFrame())
        return false;
    if (m_contextState != CONTEXT_ACTIVE)
        return false;
    return true;
}

bool SchedulerStateMachine::shouldAcquireLayerTexturesForMainThread() const
{
    if (!m_mainThreadNeedsLayerTextures)
        return false;
    if (m_textureState == LAYER_TEXTURE_STATE_UNLOCKED)
        return true;
    DCHECK(m_textureState == LAYER_TEXTURE_STATE_ACQUIRED_BY_IMPL_THREAD);
    // Transfer the lock from impl thread to main thread immediately if the
    // impl thread is not even scheduled to draw. Guards against deadlocking.
    if (!scheduledToDraw())
        return true;
    if (!vsyncCallbackNeeded())
        return true;
    return false;
}

SchedulerStateMachine::Action SchedulerStateMachine::nextAction() const
{
    if (shouldAcquireLayerTexturesForMainThread())
        return ACTION_ACQUIRE_LAYER_TEXTURES_FOR_MAIN_THREAD;
    switch (m_commitState) {
    case COMMIT_STATE_IDLE:
        if (m_contextState != CONTEXT_ACTIVE && m_needsForcedRedraw)
            return ACTION_DRAW_FORCED;
        if (m_contextState != CONTEXT_ACTIVE && m_needsForcedCommit)
            return ACTION_BEGIN_FRAME;
        if (m_contextState == CONTEXT_LOST)
            return ACTION_BEGIN_CONTEXT_RECREATION;
        if (m_contextState == CONTEXT_RECREATING)
            return ACTION_NONE;
        if (shouldDraw())
            return m_needsForcedRedraw ? ACTION_DRAW_FORCED : ACTION_DRAW_IF_POSSIBLE;
        if (m_needsCommit && ((m_visible && m_canBeginFrame) || m_needsForcedCommit))
            return ACTION_BEGIN_FRAME;
        return ACTION_NONE;

    case COMMIT_STATE_FRAME_IN_PROGRESS:
        if (shouldDraw())
            return m_needsForcedRedraw ? ACTION_DRAW_FORCED : ACTION_DRAW_IF_POSSIBLE;
        return ACTION_NONE;

    case COMMIT_STATE_READY_TO_COMMIT:
        return ACTION_COMMIT;

    case COMMIT_STATE_WAITING_FOR_FIRST_DRAW:
        if (shouldDraw() || m_contextState == CONTEXT_LOST)
            return m_needsForcedRedraw ? ACTION_DRAW_FORCED : ACTION_DRAW_IF_POSSIBLE;
        // COMMIT_STATE_WAITING_FOR_FIRST_DRAW wants to enforce a draw. If m_canDraw is false
        // or textures are not available, proceed to the next step (similar as in COMMIT_STATE_IDLE).
        bool canCommit = m_visible || m_needsForcedCommit;
        if (m_needsCommit && canCommit && drawSuspendedUntilCommit())
            return ACTION_BEGIN_FRAME;
        return ACTION_NONE;
    }
    NOTREACHED();
    return ACTION_NONE;
}

void SchedulerStateMachine::updateState(Action action)
{
    switch (action) {
    case ACTION_NONE:
        return;

    case ACTION_BEGIN_FRAME:
        DCHECK(m_visible || m_needsForcedCommit);
        m_commitState = COMMIT_STATE_FRAME_IN_PROGRESS;
        m_needsCommit = false;
        m_needsForcedCommit = false;
        return;

    case ACTION_COMMIT:
        m_commitState = COMMIT_STATE_WAITING_FOR_FIRST_DRAW;
        m_needsRedraw = true;
        if (m_drawIfPossibleFailed)
            m_lastFrameNumberWhereDrawWasCalled = -1;

        if (m_needsForcedRedrawAfterNextCommit) {
            m_needsForcedRedrawAfterNextCommit = false;
            m_needsForcedRedraw = true;
        }

        m_textureState = LAYER_TEXTURE_STATE_ACQUIRED_BY_IMPL_THREAD;
        return;

    case ACTION_DRAW_FORCED:
    case ACTION_DRAW_IF_POSSIBLE:
        m_needsRedraw = false;
        m_needsForcedRedraw = false;
        m_drawIfPossibleFailed = false;
        if (m_insideVSync)
            m_lastFrameNumberWhereDrawWasCalled = m_currentFrameNumber;
        if (m_commitState == COMMIT_STATE_WAITING_FOR_FIRST_DRAW) {
            if (m_expectImmediateBeginFrame) {
                m_commitState = COMMIT_STATE_FRAME_IN_PROGRESS;
                m_expectImmediateBeginFrame = false;
            } else
                m_commitState = COMMIT_STATE_IDLE;
        }
        if (m_textureState == LAYER_TEXTURE_STATE_ACQUIRED_BY_IMPL_THREAD)
            m_textureState = LAYER_TEXTURE_STATE_UNLOCKED;
        return;

    case ACTION_BEGIN_CONTEXT_RECREATION:
        DCHECK(m_commitState == COMMIT_STATE_IDLE);
        DCHECK(m_contextState == CONTEXT_LOST);
        m_contextState = CONTEXT_RECREATING;
        return;

    case ACTION_ACQUIRE_LAYER_TEXTURES_FOR_MAIN_THREAD:
        m_textureState = LAYER_TEXTURE_STATE_ACQUIRED_BY_MAIN_THREAD;
        m_mainThreadNeedsLayerTextures = false;
        if (m_commitState != COMMIT_STATE_FRAME_IN_PROGRESS)
            m_needsCommit = true;
        return;
    }
}

void SchedulerStateMachine::setMainThreadNeedsLayerTextures()
{
    DCHECK(!m_mainThreadNeedsLayerTextures);
    DCHECK(m_textureState != LAYER_TEXTURE_STATE_ACQUIRED_BY_MAIN_THREAD);
    m_mainThreadNeedsLayerTextures = true;
}

bool SchedulerStateMachine::vsyncCallbackNeeded() const
{
    // If we can't draw, don't tick until we are notified that we can draw again.
    if (!m_canDraw)
        return false;

    if (m_needsForcedRedraw)
        return true;

    return m_needsRedraw && m_visible && m_contextState == CONTEXT_ACTIVE;
}

void SchedulerStateMachine::didEnterVSync()
{
    m_insideVSync = true;
}

void SchedulerStateMachine::didLeaveVSync()
{
    m_currentFrameNumber++;
    m_insideVSync = false;
}

void SchedulerStateMachine::setVisible(bool visible)
{
    m_visible = visible;
}

void SchedulerStateMachine::setNeedsRedraw()
{
    m_needsRedraw = true;
}

void SchedulerStateMachine::setNeedsForcedRedraw()
{
    m_needsForcedRedraw = true;
}

void SchedulerStateMachine::didDrawIfPossibleCompleted(bool success)
{
    m_drawIfPossibleFailed = !success;
    if (m_drawIfPossibleFailed) {
        m_needsRedraw = true;
        m_needsCommit = true;
        m_consecutiveFailedDraws++;
        if (m_consecutiveFailedDraws >= m_maximumNumberOfFailedDrawsBeforeDrawIsForced) {
            m_consecutiveFailedDraws = 0;
            // We need to force a draw, but it doesn't make sense to do this until
            // we've committed and have new textures.
            m_needsForcedRedrawAfterNextCommit = true;
        }
    } else
      m_consecutiveFailedDraws = 0;
}

void SchedulerStateMachine::setNeedsCommit()
{
    m_needsCommit = true;
}

void SchedulerStateMachine::setNeedsForcedCommit()
{
    m_needsForcedCommit = true;
    m_expectImmediateBeginFrame = true;
}

void SchedulerStateMachine::beginFrameComplete()
{
    DCHECK(m_commitState == COMMIT_STATE_FRAME_IN_PROGRESS ||
           (m_expectImmediateBeginFrame && m_commitState != COMMIT_STATE_IDLE)) << toString();
    m_commitState = COMMIT_STATE_READY_TO_COMMIT;
}

void SchedulerStateMachine::beginFrameAborted()
{
    DCHECK(m_commitState == COMMIT_STATE_FRAME_IN_PROGRESS);
    if (m_expectImmediateBeginFrame)
        m_expectImmediateBeginFrame = false;
    else {
        m_commitState = COMMIT_STATE_IDLE;
        setNeedsCommit();
    }
}

void SchedulerStateMachine::didLoseContext()
{
    if (m_contextState == CONTEXT_LOST || m_contextState == CONTEXT_RECREATING)
        return;
    m_contextState = CONTEXT_LOST;
}

void SchedulerStateMachine::didRecreateContext()
{
    DCHECK(m_contextState == CONTEXT_RECREATING);
    m_contextState = CONTEXT_ACTIVE;
    setNeedsCommit();
}

void SchedulerStateMachine::setMaximumNumberOfFailedDrawsBeforeDrawIsForced(int numDraws)
{
    m_maximumNumberOfFailedDrawsBeforeDrawIsForced = numDraws;
}

}  // namespace cc
