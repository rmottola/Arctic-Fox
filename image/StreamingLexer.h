/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * StreamingLexer is a lexing framework designed to make it simple to write
 * image decoders without worrying about the details of how the data is arriving
 * from the network.
 */

#ifndef mozilla_image_StreamingLexer_h
#define mozilla_image_StreamingLexer_h

#include <algorithm>
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/Maybe.h"
#include "mozilla/Variant.h"
#include "mozilla/Vector.h"

namespace mozilla {
namespace image {

/// Buffering behaviors for StreamingLexer transitions.
enum class BufferingStrategy
{
  BUFFERED,   // Data will be buffered and processed in one chunk.
  UNBUFFERED  // Data will be processed as it arrives, in multiple chunks.
};

/// The result of a call to StreamingLexer::Lex().
enum class TerminalState
{
  SUCCESS,
  FAILURE
};

/**
 * LexerTransition is a type used to give commands to the lexing framework.
 * Code that uses StreamingLexer can create LexerTransition values using the
 * static methods on Transition, and then return them to the lexing framework
 * for execution.
 */
template <typename State>
class LexerTransition
{
public:
  // This is implicit so that Terminate{Success,Failure}() can return a
  // TerminalState and have it implicitly converted to a
  // LexerTransition<State>, which avoids the need for a "<State>"
  // qualification to the Terminate{Success,Failure}() callsite.
  MOZ_IMPLICIT LexerTransition(TerminalState aFinalState)
    : mNextState(aFinalState)
  {}

  bool NextStateIsTerminal() const
  {
    return mNextState.template is<TerminalState>();
  }

  TerminalState NextStateAsTerminal() const
  {
    return mNextState.template as<TerminalState>();
  }

  State NextState() const
  {
    return mNextState.template as<NonTerminalState>().mState;
  }

  State UnbufferedState() const
  {
    return *mNextState.template as<NonTerminalState>().mUnbufferedState;
  }

  size_t Size() const
  {
    return mNextState.template as<NonTerminalState>().mSize;
  }

  BufferingStrategy Buffering() const
  {
    return mNextState.template as<NonTerminalState>().mBufferingStrategy;
  }

private:
  friend struct Transition;

  LexerTransition(State aNextState,
                  const Maybe<State>& aUnbufferedState,
                  size_t aSize,
                  BufferingStrategy aBufferingStrategy)
    : mNextState(NonTerminalState(aNextState, aUnbufferedState, aSize,
                                  aBufferingStrategy))
  {}

  struct NonTerminalState
  {
    State mState;
    Maybe<State> mUnbufferedState;
    size_t mSize;
    BufferingStrategy mBufferingStrategy;

    NonTerminalState(State aState,
                     const Maybe<State>& aUnbufferedState,
                     size_t aSize,
                     BufferingStrategy aBufferingStrategy)
      : mState(aState)
      , mUnbufferedState(aUnbufferedState)
      , mSize(aSize)
      , mBufferingStrategy(aBufferingStrategy)
    {
      MOZ_ASSERT_IF(mBufferingStrategy == BufferingStrategy::UNBUFFERED,
                    mUnbufferedState);
      MOZ_ASSERT_IF(mUnbufferedState,
                    mBufferingStrategy == BufferingStrategy::UNBUFFERED);
    }
  };
  Variant<NonTerminalState, TerminalState> mNextState;
};

struct Transition
{
  /// Transition to @aNextState, buffering @aSize bytes of data.
  template <typename State>
  static LexerTransition<State>
  To(const State& aNextState, size_t aSize)
  {
    return LexerTransition<State>(aNextState, Nothing(), aSize,
                                  BufferingStrategy::BUFFERED);
  }

  /**
   * Transition to @aNextState via @aUnbufferedState, reading @aSize bytes of
   * data unbuffered.
   *
   * The unbuffered data will be delivered in state @aUnbufferedState, which may
   * be invoked repeatedly until all @aSize bytes have been delivered. Then,
   * @aNextState will be invoked with no data. No state transitions are allowed
   * from @aUnbufferedState except for transitions to a terminal state, so
   * @aNextState will always be reached unless lexing terminates early.
   */
  template <typename State>
  static LexerTransition<State>
  ToUnbuffered(const State& aNextState,
               const State& aUnbufferedState,
               size_t aSize)
  {
    return LexerTransition<State>(aNextState, Some(aUnbufferedState), aSize,
                                  BufferingStrategy::UNBUFFERED);
  }

  /**
   * Continue receiving unbuffered data. @aUnbufferedState should be the same
   * state as the @aUnbufferedState specified in the preceding call to
   * ToUnbuffered().
   *
   * This should be used during an unbuffered read initiated by ToUnbuffered().
   */
  template <typename State>
  static LexerTransition<State>
  ContinueUnbuffered(const State& aUnbufferedState)
  {
    return LexerTransition<State>(aUnbufferedState, Nothing(), 0,
                                  BufferingStrategy::BUFFERED);
  }

  /**
   * Terminate lexing, ending up in terminal state SUCCESS. (The implicit
   * LexerTransition constructor will convert the result to a LexerTransition
   * as needed.)
   *
   * No more data will be delivered after this function is used.
   */
  static TerminalState
  TerminateSuccess()
  {
    return TerminalState::SUCCESS;
  }

  /**
   * Terminate lexing, ending up in terminal state FAILURE. (The implicit
   * LexerTransition constructor will convert the result to a LexerTransition
   * as needed.)
   *
   * No more data will be delivered after this function is used.
   */
  static TerminalState
  TerminateFailure()
  {
    return TerminalState::FAILURE;
  }

private:
  Transition();
};

/**
 * StreamingLexer is a lexing framework designed to make it simple to write
 * image decoders without worrying about the details of how the data is arriving
 * from the network.
 *
 * To use StreamingLexer:
 *
 *  - Create a State type. This should be an |enum class| listing all of the
 *    states that you can be in while lexing the image format you're trying to
 *    read.
 *
 *  - Add an instance of StreamingLexer<State> to your decoder class. Initialize
 *    it with a Transition::To() the state that you want to start lexing in.
 *
 *  - In your decoder's DoDecode() method, call Lex(), passing in the input
 *    data and length that are passed to DoDecode(). You also need to pass
 *    a lambda which dispatches to lexing code for each state based on the State
 *    value that's passed in. The lambda generally should just continue a
 *    |switch| statement that calls different methods for each State value. Each
 *    method should return a LexerTransition<State>, which the lambda should
 *    return in turn.
 *
 *  - Write the methods that actually implement lexing for your image format.
 *    These methods should return either Transition::To(), to move on to another
 *    state, or Transition::Terminate{Success,Failure}(), if lexing has
 *    terminated in either success or failure. (There are also additional
 *    transitions for unbuffered reads; see below.)
 *
 * That's all there is to it. The StreamingLexer will track your position in the
 * input and buffer enough data so that your lexing methods can process
 * everything in one pass. Lex() returns Nothing() if more data is needed, in
 * which case you should just return from DoDecode(). If lexing reaches a
 * terminal state, Lex() returns Some(State::SUCCESS) or Some(State::FAILURE),
 * and you can check which one to determine if lexing succeeded or failed and do
 * any necessary cleanup.
 *
 * There's one more wrinkle: some lexers may want to *avoid* buffering in some
 * cases, and just process the data as it comes in. This is useful if, for
 * example, you just want to skip over a large section of data; there's no point
 * in buffering data you're just going to ignore.
 *
 * You can begin an unbuffered read with Transition::ToUnbuffered(). This works
 * a little differently than Transition::To() in that you specify *two* states.
 * The @aUnbufferedState argument specifies a state that will be called
 * repeatedly with unbuffered data, as soon as it arrives. The implementation
 * for that state should return either a transition to a terminal state, or
 * Transition::ContinueUnbuffered(). Once the amount of data requested in the
 * original call to Transition::ToUnbuffered() has been delivered, Lex() will
 * transition to the @aNextState state specified via Transition::ToUnbuffered().
 * That state will be invoked with *no* data; it's just called to signal that
 * the unbuffered read is over.
 *
 * XXX(seth): We should be able to get of the |State| stuff totally once bug
 * 1198451 lands, since we can then just return a function representing the next
 * state directly.
 */
template <typename State, size_t InlineBufferSize = 16>
class StreamingLexer
{
public:
  explicit StreamingLexer(LexerTransition<State> aStartState)
    : mTransition(aStartState)
    , mToReadUnbuffered(0)
  { }

  template <typename Func>
  Maybe<TerminalState> Lex(const char* aInput, size_t aLength, Func aFunc)
  {
    if (mTransition.NextStateIsTerminal()) {
      // We've already reached a terminal state. We never deliver any more data
      // in this case; just return the terminal state again immediately.
      return Some(mTransition.NextStateAsTerminal());
    }

    if (mToReadUnbuffered > 0) {
      // We're continuing an unbuffered read.

      MOZ_ASSERT(mBuffer.empty(),
                 "Shouldn't be continuing an unbuffered read and a buffered "
                 "read at the same time");

      size_t toRead = std::min(mToReadUnbuffered, aLength);

      // Call aFunc with the unbuffered state to indicate that we're in the
      // middle of an unbuffered read. We enforce that any state transition
      // passed back to us is either a terminal state or takes us back to the
      // unbuffered state.
      LexerTransition<State> unbufferedTransition =
        aFunc(mTransition.UnbufferedState(), aInput, toRead);
      if (unbufferedTransition.NextStateIsTerminal()) {
        mTransition = unbufferedTransition;
        return Some(mTransition.NextStateAsTerminal());  // Done!
      }
      MOZ_ASSERT(mTransition.UnbufferedState() ==
                   unbufferedTransition.NextState());

      aInput += toRead;
      aLength -= toRead;
      mToReadUnbuffered -= toRead;
      if (mToReadUnbuffered != 0) {
        return Nothing();  // Need more input.
      }

      // We're done with the unbuffered read, so transition to the next state.
      mTransition = aFunc(mTransition.NextState(), nullptr, 0);
      if (mTransition.NextStateIsTerminal()) {
        return Some(mTransition.NextStateAsTerminal());  // Done!
      }
    } else if (0 < mBuffer.length()) {
      // We're continuing a buffered read.

      MOZ_ASSERT(mToReadUnbuffered == 0,
                 "Shouldn't be continuing an unbuffered read and a buffered "
                 "read at the same time");
      MOZ_ASSERT(mBuffer.length() < mTransition.Size(),
                 "Buffered more than we needed?");

      size_t toRead = std::min(aLength, mTransition.Size() - mBuffer.length());

      if (!mBuffer.append(aInput, toRead)) {
        return Some(TerminalState::FAILURE);
      }
      aInput += toRead;
      aLength -= toRead;
      if (mBuffer.length() != mTransition.Size()) {
        return Nothing();  // Need more input.
      }

      // We've buffered everything, so transition to the next state.
      mTransition =
        aFunc(mTransition.NextState(), mBuffer.begin(), mBuffer.length());
      mBuffer.clear();
      if (mTransition.NextStateIsTerminal()) {
        return Some(mTransition.NextStateAsTerminal());  // Done!
      }
    }

    MOZ_ASSERT(mToReadUnbuffered == 0);
    MOZ_ASSERT(mBuffer.empty());

    // Process states as long as we continue to have enough input to do so.
    while (mTransition.Size() <= aLength) {
      size_t toRead = mTransition.Size();

      if (mTransition.Buffering() == BufferingStrategy::BUFFERED) {
        mTransition = aFunc(mTransition.NextState(), aInput, toRead);
      } else {
        MOZ_ASSERT(mTransition.Buffering() == BufferingStrategy::UNBUFFERED);

        // Call aFunc with the unbuffered state to indicate that we're in the
        // middle of an unbuffered read. We enforce that any state transition
        // passed back to us is either a terminal state or takes us back to the
        // unbuffered state.
        LexerTransition<State> unbufferedTransition =
          aFunc(mTransition.UnbufferedState(), aInput, toRead);
        if (unbufferedTransition.NextStateIsTerminal()) {
          mTransition = unbufferedTransition;
          return Some(mTransition.NextStateAsTerminal());  // Done!
        }
        MOZ_ASSERT(mTransition.UnbufferedState() ==
                     unbufferedTransition.NextState());

        // We're done with the unbuffered read, so transition to the next state.
        mTransition = aFunc(mTransition.NextState(), nullptr, 0);
      }

      aInput += toRead;
      aLength -= toRead;

      if (mTransition.NextStateIsTerminal()) {
        return Some(mTransition.NextStateAsTerminal());  // Done!
      }
    }

    if (aLength == 0) {
      // We finished right at a transition point. Just wait for more data.
      return Nothing();
    }

    // If the next state is unbuffered, deliver what we can and then wait.
    if (mTransition.Buffering() == BufferingStrategy::UNBUFFERED) {
      LexerTransition<State> unbufferedTransition =
        aFunc(mTransition.UnbufferedState(), aInput, aLength);
      if (unbufferedTransition.NextStateIsTerminal()) {
        mTransition = unbufferedTransition;
        return Some(mTransition.NextStateAsTerminal());  // Done!
      }
      MOZ_ASSERT(mTransition.UnbufferedState() ==
                   unbufferedTransition.NextState());

      mToReadUnbuffered = mTransition.Size() - aLength;
      return Nothing();  // Need more input.
    }

    // If the next state is buffered, buffer what we can and then wait.
    MOZ_ASSERT(mTransition.Buffering() == BufferingStrategy::BUFFERED);
    if (!mBuffer.reserve(mTransition.Size())) {
      return Some(TerminalState::FAILURE);  // Done due to allocation failure.
    }
    if (!mBuffer.append(aInput, aLength)) {
      return Some(TerminalState::FAILURE);
    }
    return Nothing();  // Need more input.
  }

private:
  Vector<char, InlineBufferSize> mBuffer;
  LexerTransition<State> mTransition;
  size_t mToReadUnbuffered;
};

} // namespace image
} // namespace mozilla

#endif // mozilla_image_StreamingLexer_h
