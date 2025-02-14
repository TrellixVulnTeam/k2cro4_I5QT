// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_SCHEDULER_STATE_MACHINE_H_
#define CC_SCHEDULER_STATE_MACHINE_H_

#include <string>

#include "base/basictypes.h"
#include "cc/cc_export.h"

namespace cc {

// The SchedulerStateMachine decides how to coordinate main thread activites
// like painting/running javascript with rendering and input activities on the
// impl thread.
//
// The state machine tracks internal state but is also influenced by external state.
// Internal state includes things like whether a frame has been requested, while
// external state includes things like the current time being near to the vblank time.
//
// The scheduler seperates "what to do next" from the updating of its internal state to
// make testing cleaner.
class CC_EXPORT SchedulerStateMachine {
public:
    SchedulerStateMachine();

    enum CommitState {
        COMMIT_STATE_IDLE,
        COMMIT_STATE_FRAME_IN_PROGRESS,
        COMMIT_STATE_READY_TO_COMMIT,
        COMMIT_STATE_WAITING_FOR_FIRST_DRAW,
    };

    enum TextureState {
        LAYER_TEXTURE_STATE_UNLOCKED,
        LAYER_TEXTURE_STATE_ACQUIRED_BY_MAIN_THREAD,
        LAYER_TEXTURE_STATE_ACQUIRED_BY_IMPL_THREAD,
    };

    enum ContextState {
        CONTEXT_ACTIVE,
        CONTEXT_LOST,
        CONTEXT_RECREATING,
    };

    bool commitPending() const
    {
        return m_commitState != COMMIT_STATE_IDLE;
    }

    bool redrawPending() const { return m_needsRedraw; }

    enum Action {
        ACTION_NONE,
        ACTION_BEGIN_FRAME,
        ACTION_COMMIT,
        ACTION_DRAW_IF_POSSIBLE,
        ACTION_DRAW_FORCED,
        ACTION_BEGIN_CONTEXT_RECREATION,
        ACTION_ACQUIRE_LAYER_TEXTURES_FOR_MAIN_THREAD,
    };
    Action nextAction() const;
    void updateState(Action);

    // Indicates whether the scheduler needs a vsync callback in order to make
    // progress.
    bool vsyncCallbackNeeded() const;

    // Indicates that the system has entered and left a vsync callback.
    // The scheduler will not draw more than once in a given vsync callback.
    void didEnterVSync();
    void didLeaveVSync();

    // Indicates whether the LayerTreeHostImpl is visible.
    void setVisible(bool);

    // Indicates that a redraw is required, either due to the impl tree changing
    // or the screen being damaged and simply needing redisplay.
    void setNeedsRedraw();

    // As setNeedsRedraw(), but ensures the draw will definitely happen even if
    // we are not visible.
    void setNeedsForcedRedraw();

    // Indicates whether ACTION_DRAW_IF_POSSIBLE drew to the screen or not.
    void didDrawIfPossibleCompleted(bool success);

    // Indicates that a new commit flow needs to be performed, either to pull
    // updates from the main thread to the impl, or to push deltas from the impl
    // thread to main.
    void setNeedsCommit();

    // As setNeedsCommit(), but ensures the beginFrame will definitely happen even if
    // we are not visible.
    // After this call we expect to go through the forced commit flow and then return
    // to waiting for a non-forced beginFrame to finish.
    void setNeedsForcedCommit();

    // Call this only in response to receiving an ACTION_BEGIN_FRAME
    // from nextState. Indicates that all painting is complete.
    void beginFrameComplete();

    // Call this only in response to receiving an ACTION_BEGIN_FRAME
    // from nextState if the client rejects the beginFrame message.
    void beginFrameAborted();

    // Request exclusive access to the textures that back single buffered
    // layers on behalf of the main thread. Upon acqusition,
    // ACTION_DRAW_IF_POSSIBLE will not draw until the main thread releases the
    // textures to the impl thread by committing the layers.
    void setMainThreadNeedsLayerTextures();

    // Indicates whether we can successfully begin a frame at this time.
    void setCanBeginFrame(bool can) { m_canBeginFrame = can; }

    // Indicates whether drawing would, at this time, make sense.
    // canDraw can be used to supress flashes or checkerboarding
    // when such behavior would be undesirable.
    void setCanDraw(bool can) { m_canDraw = can; }

    void didLoseContext();
    void didRecreateContext();

    // Exposed for testing purposes.
    void setMaximumNumberOfFailedDrawsBeforeDrawIsForced(int);

    std::string toString();

protected:
    bool shouldDrawForced() const;
    bool drawSuspendedUntilCommit() const;
    bool scheduledToDraw() const;
    bool shouldDraw() const;
    bool shouldAcquireLayerTexturesForMainThread() const;
    bool hasDrawnThisFrame() const;

    CommitState m_commitState;

    int m_currentFrameNumber;
    int m_lastFrameNumberWhereDrawWasCalled;
    int m_consecutiveFailedDraws;
    int m_maximumNumberOfFailedDrawsBeforeDrawIsForced;
    bool m_needsRedraw;
    bool m_needsForcedRedraw;
    bool m_needsForcedRedrawAfterNextCommit;
    bool m_needsCommit;
    bool m_needsForcedCommit;
    bool m_expectImmediateBeginFrame;
    bool m_mainThreadNeedsLayerTextures;
    bool m_insideVSync;
    bool m_visible;
    bool m_canBeginFrame;
    bool m_canDraw;
    bool m_drawIfPossibleFailed;
    TextureState m_textureState;
    ContextState m_contextState;

    DISALLOW_COPY_AND_ASSIGN(SchedulerStateMachine);
};

}

#endif  // CC_SCHEDULER_STATE_MACHINE_H_
