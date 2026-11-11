/**
 * @file ZeSelfAction.h
 * @brief Event callback protocol: lets a callback receive the widget that raised the event -- the single copy in the whole library, independent of any concrete widget
 *
 * @details
 * This header solves two problems: one old problem and one architectural problem.
 *
 * ---------------------------------------------------------------------------
 *  Problem 1 (old): the "moving" pitfall of value-semantics libraries
 * ---------------------------------------------------------------------------
 * Before mounting (AddWidget), a widget is moved 1~2 times (local object -> `return x;` ->
 * `make_unique`). When developers capture a widget reference during configuration (`[&a]{...}` / `[p=&a]{...}`),
 * what they capture is the box on the stack that will become invalid, not the final entity in the tree.
 * After mounting the entity address is stable,
 * but by then the callback already holds a dead address -- the compiler reports no error, and the symptom is "an empty string after clicking".
 *
 * Solution: `self` is not captured by the developer; the library injects it at dispatch time (`onClick_(*this)`).
 * Dispatch only happens after the widget is mounted, so `*this` is necessarily the entity in the tree, no matter how many times it was moved.
 *
 * ---------------------------------------------------------------------------
 *  Problem 2 (architecture): the protocol must not be "one copy per widget"
 * ---------------------------------------------------------------------------
 * The old approach was for each widget to declare `using SelfAction = SelfActionT<Button>;` inside its class --
 * so `Button` / `InputBox` / `ScrollView` / `LayoutBase` each had a protocol that looked similar but
 * was mutually incompatible. The consequences: every new widget required copying it again, and developers switching widgets
 * had to re-check the signature.
 *
 * Now there is a single copy: the callback's "sender" parameter is always `Widget&` (the base class),
 * independent of the concrete widget. Therefore:
 *   - Any widget (including ones added later) supports this protocol inherently, with no `using` line needed;
 *   - Dispatch points inside the library only need to pass `*this`, and polymorphism upcasts it to `Widget&` automatically;
 *   - When a concrete interface is needed (`Text()` / `SetScrollY()` ...), cast with `Kit::As<T>()`.
 *
 * ---------------------------------------------------------------------------
 *  Three signature tiers (split by whether a payload is carried, not by widget)
 * ---------------------------------------------------------------------------
 *  | Event form             | What to use                    | Example |
 *  |---|---|---|
 *  | Sender not needed, no data | the widget's own parameterless `std::function<void()>` | the parameterless overload of `Button::SetOnClick` |
 *  | Sender needed, no data    | `SelfAction`             | `SetOnClick(SelfAction)` |
 *  | Sender needed, one payload | `SelfAction1<P>`       | `InputBox::SetOnTextChanged` |
 *  | Sender needed, two payloads | `SelfAction2<P1,P2>`   | `ScrollView::SetOnScroll` |
 *
 * More parameters than that means an event struct should be defined -- this is the upper limit at which the function signature is still readable.
 *
 * @see ZeWidget.h  where As<T>() is defined (it needs the complete Widget definition, so it cannot live in this file)
 */

#pragma once
#include <functional>

namespace Kit {
    class Widget;   /// < A forward declaration suffices: std::function<void(Widget&)> does not need the complete definition

    /**
     * @brief Event callback (with sender, without data)
     *
     * @details
     * The `Widget&` parameter is the widget that raised the event; the library injects it at dispatch time, and it is always the entity in the tree.
     * Developers do not need to capture the widget (and therefore do not need to understand move semantics):
     *
     * @code
     * btn.SetOnClick([](Kit::Widget& self) {
     *     // self is always valid. Cast for a concrete interface:
     *     std::cout << Kit::As<Kit::Button>(self).Text() << std::endl;
     * });
     * @endcode
     *
     * @note There is only this one definition in the whole library -- new widgets do not need to (and must not) define their own alias.
     */
    using SelfAction = std::function<void(Widget&)>;

    /**
     * @brief Event callback (with sender, carrying one payload)
     *
     * @tparam P payload type (e.g. `const std::string&`, `float`)
     *
     * @details
     * For cases where the event naturally carries data (text changes in an input box, etc.). `P` is usually written as a reference to save a copy:
     *
     * @code
     * input.SetOnTextChanged([](Kit::Widget& self, const std::string& text) {
     *     Kit::As<Kit::InputBox>(self).SetPlaceholder(text + " chars");
     * });
     * @endcode
     */
    template <class P>
    using SelfAction1 = std::function<void(Widget&, P)>;

    /**
     * @brief Event callback (with sender, carrying two payloads)
     *
     * @details
     * For cases where one event carries two values (the scroll x/y of `ScrollView`).
     */
    template <class P1, class P2>
    using SelfAction2 = std::function<void(Widget&, P1, P2)>;

} // namespace Kit
