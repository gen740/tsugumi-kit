# Clear a Dawn view

Run the `color` Card to open its `clear` view. The C++ Card sends the three
RGB values from `params.nix` to the view session. Click within eight seconds
to send the position to the `color` Card and record it in the run log. The
view keeps showing the colour until you close it.

The view uses the 48 kHz clock signatures for resize, draw, messages and
pointer input. See the [Dawn SDK guide][dawn-sdk] for optional
audio, keyboard, text and gamepad exports and the embedded font helper.

[dawn-sdk]: https://github.com/gen740/tsugumi-kit/blob/main/docs/dawn-sdk.md
