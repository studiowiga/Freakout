// genere par tools/make_background.py - ne pas modifier a la main (repere 760 x 958)
#pragma once
#include <juce_graphics/juce_graphics.h>
namespace layout
{
    // ellipses de perspective (face superieure vue par la photo) : centre, demi-axes
    const juce::Point<float> capC { 508.48f, 163.82f }, skirtC { 510.14f, 168.81f }, typeC { 510.56f, 353.43f };
    const float capRx = 32.43f, capRy = 31.60f, skirtRx = 55.30f, skirtRy = 54.05f, typeRx = 38.25f, typeRy = 36.59f;
    const juce::Point<float> capBaseC { 509.31f, 177.96f }; const float capSideR = 34.51f;
    const float unitPerPhotoPx = 0.83160f;
    const juce::Point<float> momentaryPos { 331.77f, 175.47f }, dryPos { 331.77f, 361.74f };
    const float toggleHalfW = 46.57f, toggleAbove = 41.58f, toggleBelow = 73.18f;
    const juce::Point<float> footPos { 381.66f, 745.94f }; const float footR = 42.83f;
    const juce::Rectangle<float> footRect { 323.45f, 686.90f, 118.09f, 136.38f };
    const juce::Rectangle<float> glowRect { 193.00f, 70.50f, 67.00f, 372.00f };
    const juce::Rectangle<float> rowRect { 187.07f, 470.68f, 389.19f, 36.59f };
    const juce::Rectangle<float> barRect { 184.57f, 511.85f, 394.18f, 27.03f };
}
