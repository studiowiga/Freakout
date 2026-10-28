#include "PluginEditor.h"
#include "BinaryData.h"
#include "Layout.h"

using namespace juce;

namespace
{
    // positions dans le repere 760 x 958 (fond 1520 x 1916), generees par tools/make_background.py dans Layout.h
    // a partir du visuel fourni : les potards sont vus en legere perspective (faces superieures = ellipses)
    const float rangeHalf = layout::skirtRx + 3.0f;
    const float typeHalf  = layout::typeRx + 4.0f;
    const Rectangle<float> rowRect = layout::rowRect;           // ligne du haut de l'ecran
    const Rectangle<float> presetRect = layout::barRect;        // barre de presets (son cadre fait partie du fond)
    const Rectangle<float> statusRect = rowRect;

    const float rotStart = -150.0f, rotEnd = 150.0f;    // degres, 0 = midi (repere vers le haut)
    float knobAngle (float v) { return degreesToRadians (rotStart + (rotEnd - rotStart) * jlimit (0.0f, 10.0f, v) / 10.0f); }

    const Colour inkWhite (0xffd6d6d4), lcd (0xffe9b48f);       // couleurs des textes de l'ecran du visuel

    const char* const valueIds[] = { "gain", "onset", "type", "momentary", "dry", "active", "sensitivity" };
    constexpr int numValueIds = 7;

    // point de la face superieure d'un potard (ellipse de perspective) a l'angle a et a la fraction f du rayon
    Point<float> onFace (Point<float> c, float rx, float ry, float a, float f)
    {
        return { c.x + f * rx * std::sin (a), c.y - f * ry * std::cos (a) };
    }

    // repere peint en blanc (comme sur le visuel) : la lumiere vient du haut, un peu a gauche ;
    // la peinture est un poil plus claire tournee vers elle, fine ombre de relief vers le bas
    void paintPointer (Graphics& g, Point<float> c, float rx, float ry, float a, float f0, float f1, float w, bool roundEnds)
    {
        const auto p0 = onFace (c, rx, ry, a, f0), p1 = onFace (c, rx, ry, a, f1);
        Path line; line.startNewSubPath (p0); line.lineTo (p1);
        Path s;
        PathStrokeType (w, PathStrokeType::curved, roundEnds ? PathStrokeType::rounded : PathStrokeType::butt).createStrokedPath (s, line);
        const float lightFacing = 0.5f + 0.5f * std::cos (a + MathConstants<float>::pi * 0.15f);
        g.setColour (Colours::black.withAlpha (0.50f));
        g.fillPath (s, AffineTransform::translation (0.25f, 0.55f));
        ColourGradient paint (Colour::greyLevel (0.97f), p1.x, p1.y, Colour::greyLevel (0.80f + 0.10f * lightFacing), p0.x, p0.y, false);
        g.setGradientFill (paint);
        g.fillPath (s);
        // petit reflet le long du bord tourne vers la lumiere
        g.setColour (Colours::white.withAlpha (0.35f));
        g.fillPath (s, AffineTransform::translation (-0.2f, -0.3f).scaled (1.0f, 1.0f));
    }

    // dessus visible de la jupe ONSET : ellipse de la jupe moins la silhouette du chapeau (dessus + flanc en perspective)
    Path skirtVisible()
    {
        Path p;
        p.addEllipse (layout::skirtC.x - layout::skirtRx, layout::skirtC.y - layout::skirtRy, layout::skirtRx * 2.0f, layout::skirtRy * 2.0f);
        Path cap;
        const float r = layout::capSideR + 0.6f;
        const float cx = (layout::capC.x + layout::capBaseC.x) * 0.5f;
        const float yTop = layout::capC.y, yBase = layout::capBaseC.y;
        cap.addCentredArc (cx, yTop,  r, r * layout::capRy / layout::capRx, 0.0f, -MathConstants<float>::halfPi, MathConstants<float>::halfPi, true);
        cap.lineTo (cx + r, yBase);
        cap.addCentredArc (cx, yBase, r, r * layout::capRy / layout::capRx, 0.0f, MathConstants<float>::halfPi, MathConstants<float>::pi * 1.5f, false);
        cap.closeSubPath();
        p.addPath (cap);
        p.setUsingNonZeroWinding (false);
        return p;
    }
}

//==============================================================================
// POTARD CONCENTRIQUE RANGE
//==============================================================================
ConcentricKnob::ConcentricKnob (RangedAudioParameter& g, RangedAudioParameter& o)
    : gainAtt  (g, [this] (float v) { gainV = v;  repaint(); }),
      onsetAtt (o, [this] (float v) { onsetV = v; repaint(); })
{
    gainAtt.sendInitialUpdate();
    onsetAtt.sendInitialUpdate();
    setMouseCursor (MouseCursor::PointingHandCursor);
}

bool ConcentricKnob::hitTest (int x, int y)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    return c.getDistanceFrom ({ (float) x, (float) y }) <= layout::skirtRx * unitScale;
}

int ConcentricKnob::zoneAt (Point<float> p) const
{
    const auto c = getLocalBounds().toFloat().getCentre();
    // chapeau GAIN : sa face superieure est decalee vers le haut (perspective)
    const Point<float> capC ((layout::capC.x - layout::skirtC.x) * unitScale + c.x, (layout::capC.y + 4.0f - layout::skirtC.y) * unitScale + c.y);
    return capC.getDistanceFrom (p) <= (layout::capRx + 3.0f) * unitScale ? 0 : 1;
}

void ConcentricKnob::setV (int which, float v)
{
    v = jlimit (0.0f, 10.0f, v);
    if (which == 0) gainAtt.setValueAsPartOfGesture (v);
    else            onsetAtt.setValueAsPartOfGesture (v);
    if (onUserChange) onUserChange();
    if (onTouched) onTouched (which);
}

void ConcentricKnob::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
    dragging = zoneAt (e.position);
    startV = valueOf (dragging);
    if (dragging == 0) gainAtt.beginGesture(); else onsetAtt.beginGesture();
    if (onTouched) onTouched (dragging);
}

void ConcentricKnob::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0) return;
    const float sens = e.mods.isShiftDown() ? 900.0f : 220.0f;     // Maj = reglage fin
    const float d = (float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY());
    setV (dragging, startV + d * 10.0f / sens);
}

void ConcentricKnob::mouseUp (const MouseEvent&)
{
    if (dragging == 0) gainAtt.endGesture(); else if (dragging == 1) onsetAtt.endGesture();
    dragging = -1;
}

void ConcentricKnob::mouseDoubleClick (const MouseEvent& e)
{
    const int z = zoneAt (e.position);
    if (z == 0) gainAtt.setValueAsCompleteGesture (5.0f); else onsetAtt.setValueAsCompleteGesture (5.0f);
    if (onUserChange) onUserChange();
    if (onTouched) onTouched (z);
}

void ConcentricKnob::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const int z = zoneAt (e.position);
    const float v = valueOf (z) + (w.deltaY > 0 ? 0.2f : -0.2f) * (e.mods.isShiftDown() ? 0.25f : 1.0f);
    if (z == 0) gainAtt.setValueAsCompleteGesture (jlimit (0.0f, 10.0f, v));
    else        onsetAtt.setValueAsCompleteGesture (jlimit (0.0f, 10.0f, v));
    if (onUserChange) onUserChange();
    if (onTouched) onTouched (z);
}

void ConcentricKnob::paint (Graphics&) {}       // reperes dessines par l'editeur (perspective, decoupe par le chapeau)

//==============================================================================
// SELECTEUR TYPE
//==============================================================================
TypeKnob::TypeKnob (RangedAudioParameter& p)
    : attachment (p, [this] (float v)
                  {
                      pos = jlimit (0, 6, roundToInt (v));
                      targetAngle = degreesToRadians (-135.0f + 45.0f * (float) pos);
                      repaint();
                  })
{
    attachment.sendInitialUpdate();
    shownAngle = targetAngle;
    setMouseCursor (MouseCursor::PointingHandCursor);
}

bool TypeKnob::hitTest (int x, int y)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    return c.getDistanceFrom ({ (float) x, (float) y }) <= (float) getWidth() * 0.5f;
}

void TypeKnob::moveTo (int p)
{
    p = jlimit (0, 6, p);
    if (p == pos) return;
    attachment.setValueAsCompleteGesture ((float) p);
    if (onUserChange) onUserChange();
}

void TypeKnob::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
    dragStartPos = pos;
}

void TypeKnob::mouseDrag (const MouseEvent& e)
{
    const int d = (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / 24;
    moveTo (dragStartPos + d);
}

void TypeKnob::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    if (std::abs (w.deltaY) > 0.0f) moveTo (pos + (w.deltaY > 0 ? 1 : -1));
}

bool TypeKnob::tick()
{
    // le selecteur tourne d'un cran a l'autre en ~70 ms (pas de saut)
    if (std::abs (targetAngle - shownAngle) < 0.002f) { if (std::abs (shownAngle - targetAngle) > 0.0f) { shownAngle = targetAngle; repaint(); return true; } return false; }
    shownAngle += (targetAngle - shownAngle) * 0.55f;
    repaint();
    return true;
}

void TypeKnob::paint (Graphics&) {}

//==============================================================================
// INVERSEUR (haut = ON)
//==============================================================================
LeverSwitch::LeverSwitch (RangedAudioParameter& p)
    : attachment (p, [this] (float v)
                  {
                      const bool nv = v > 0.5f;
                      if (nv != on)
                      {
                          on = nv;
                          targetFrame = on ? 0 : 2;
                          shownFrame = 1;                                   // position intermediaire ~55 ms
                          transitionUntil = Time::getMillisecondCounterHiRes() + 55.0;
                          repaint();
                      }
                  })
{
    attachment.sendInitialUpdate();
    targetFrame = shownFrame = on ? 0 : 2;
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void LeverSwitch::tick (double now)
{
    if (shownFrame != targetFrame && now >= transitionUntil) { shownFrame = targetFrame; repaint(); }
}

void LeverSwitch::paint (Graphics& g)
{
    if (! strip.isValid()) return;
    const int fw = strip.getWidth(), fh = strip.getHeight() / 3;
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImage (strip, 0, 0, getWidth(), getHeight(), 0, shownFrame * fh, fw, fh);
}

void LeverSwitch::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
    attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f);
    if (onUserChange) onUserChange();
}

void LeverSwitch::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    if (std::abs (w.deltaY) < 0.001f) return;
    const bool want = w.deltaY > 0;      // molette vers le haut = levier en haut (ON)
    if (want != on) { attachment.setValueAsCompleteGesture (want ? 1.0f : 0.0f); if (onUserChange) onUserChange(); }
}

//==============================================================================
// FOOTSWITCH
//==============================================================================
FootSwitchArea::FootSwitchArea (RangedAudioParameter& p, std::atomic<float>* momentary)
    : mom (momentary),
      attachment (p, [this] (float v) { value = v > 0.5f; repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor (MouseCursor::PointingHandCursor);
}

bool FootSwitchArea::hitTest (int x, int y)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    return c.getDistanceFrom ({ (float) x, (float) y }) <= (float) getWidth() * 0.5f;
}

void FootSwitchArea::paint (Graphics&) {}        // l'editeur dessine le bouton enfonce (sprite tire du visuel)

void FootSwitchArea::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
    pressed = true;
    if (onPressChange) onPressChange();
    const bool momentary = mom != nullptr && mom->load() > 0.5f;
    attachment.setValueAsCompleteGesture (momentary ? 1.0f : (value ? 0.0f : 1.0f));   // bascule des l'appui
    if (onUserChange) onUserChange();
}

void FootSwitchArea::mouseUp (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    const bool momentary = mom != nullptr && mom->load() > 0.5f;
    if (pressed && momentary)                       // MOMENTARY ON : le larsen s'arrete au relachement
    {
        attachment.setValueAsCompleteGesture (0.0f);
        if (onUserChange) onUserChange();
    }
    pressed = false;
    if (onPressChange) onPressChange();
}

//==============================================================================
// EDITEUR
//==============================================================================
FreakoutAudioProcessorEditor::FreakoutAudioProcessorEditor (FreakoutAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      range (*p.apvts.getParameter ("gain"), *p.apvts.getParameter ("onset")),
      typeKnob (*p.apvts.getParameter ("type")),
      momentarySw (*p.apvts.getParameter ("momentary")),
      drySw (*p.apvts.getParameter ("dry")),
      foot (*p.apvts.getParameter ("active"), p.apvts.getRawParameterValue ("momentary"))
{
    setLookAndFeel (&lnf);
    background = ImageCache::getFromMemory (BinaryData::background_jpg, BinaryData::background_jpgSize);
    glowStrip  = ImageCache::getFromMemory (BinaryData::glow_strip_png, BinaryData::glow_strip_pngSize);
    const auto strip = ImageCache::getFromMemory (BinaryData::toggle_strip_png, BinaryData::toggle_strip_pngSize);
    fsDown = ImageCache::getFromMemory (BinaryData::fs_down_png, BinaryData::fs_down_pngSize);
    // police du visuel (Barlow Condensed) pour l'affichage
    labelFace = Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize);
    boldFace  = Typeface::createSystemTypefaceFor (BinaryData::BarlowCondensedBold_ttf, BinaryData::BarlowCondensedBold_ttfSize);

    auto edited = [this] { proc.currentPreset = -1; };
    range.onRightClick = [this] { showContextMenu(); };
    range.onUserChange = edited;
    range.onTouched = [this] (int which) { lastChanged = which; showStatus(); };
    typeKnob.onRightClick = [this] { showContextMenu(); };
    typeKnob.onUserChange = edited;
    for (auto* l : { &momentarySw, &drySw })
    {
        l->strip = strip;
        l->onRightClick = [this] { showContextMenu(); };
        l->onUserChange = edited;
    }
    foot.onRightClick = [this] { showContextMenu(); };
    foot.onPressChange = [this] { askRepaint (scaled (layout::footRect)); };
    range.onRepaintNeeded = [this] { askRepaint (scaled (Rectangle<float> (layout::skirtC.x - rangeHalf, layout::capC.y - rangeHalf - 6.0f, rangeHalf * 2.0f, rangeHalf * 2.0f + 16.0f))); };
    addAndMakeVisible (range);
    addAndMakeVisible (typeKnob);
    addAndMakeVisible (momentarySw);
    addAndMakeVisible (drySw);
    addAndMakeVisible (foot);

    for (int i = 0; i < numValueIds; ++i) lastValues[i] = proc.raw (valueIds[i]);
    lastPresetCounter = proc.presetCounter.load();
    lastMidiCount = proc.midiCounter.load();
    ledLevel = proc.raw ("active") > 0.5f ? 1.0f : 0.0f;

    // taille par defaut = taille minimale (impossible de reduire en dessous), proportions bloquees
    const int defaultW = roundToInt ((float) defaultH * (float) baseW / (float) baseH);
    setResizable (true, true);
    setResizeLimits (defaultW, defaultH, baseW * 3 / 2, baseH * 3 / 2);
    if (auto* cons = getConstrainer())
        cons->setFixedAspectRatio ((double) baseW / (double) baseH);
    setOpaque (true);
    setSize (defaultW, defaultH);
    rebuildCaches();
    startTimerHz (30);
}

FreakoutAudioProcessorEditor::~FreakoutAudioProcessorEditor() { stopTimer(); setLookAndFeel (nullptr); }

//==============================================================================
void FreakoutAudioProcessorEditor::setScaledHeight (int h)
{
    h = jmax ((int) defaultH, h);
    setSize (roundToInt ((float) h * (float) baseW / (float) baseH), h);
}

Rectangle<int> FreakoutAudioProcessorEditor::scaled (Rectangle<float> r) const
{
    const float s = (float) getHeight() / (float) baseH;
    return r.transformedBy (AffineTransform::scale (s)).getSmallestIntegerContainer();
}

float FreakoutAudioProcessorEditor::physicalScale() const
{
    return jlimit (1.0f, 4.0f, (float) Component::getApproximateScaleFactorForComponent (this));
}

void FreakoutAudioProcessorEditor::showStatus() { statusUntil = Time::getMillisecondCounterHiRes() + 2500.0; }

//==============================================================================
// caches d'affichage : fond et calques lumineux a la taille de la fenetre (reconstruits par le timer seulement)
//==============================================================================
void FreakoutAudioProcessorEditor::rebuildCaches (float sc)
{
    if (sc <= 0.0f) sc = cacheScale > 0.0f ? cacheScale : physicalScale();
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) return;
    if (w == cacheW && h == cacheH && std::abs (sc - cacheScale) < 0.01f && bgScaled.isValid()) return;
    cacheW = w; cacheH = h; cacheScale = sc;

    const int pw = roundToInt ((float) w * sc), ph = roundToInt ((float) h * sc);
    bgScaled = (pw >= background.getWidth()) ? background : background.rescaled (pw, ph, Graphics::highResamplingQuality);

    const float s = (float) h / (float) baseH;
    const auto gr = layout::glowRect;
    const int lw = glowStrip.getWidth(), lh = glowStrip.getHeight() / 5;
    const int tw = roundToInt (gr.getWidth() * s * sc), th = roundToInt (gr.getHeight() * s * sc);
    const int fw = roundToInt (layout::footRect.getWidth() * s * sc), fh = roundToInt (layout::footRect.getHeight() * s * sc);
    fsDownS = (fw >= fsDown.getWidth()) ? fsDown : fsDown.rescaled (fw, fh, Graphics::highResamplingQuality);
    for (int k = 0; k < 5; ++k)
    {
        auto layer = glowStrip.getClippedImage ({ 0, k * lh, lw, lh });
        glowScaled[k] = (tw >= lw) ? layer.createCopy() : layer.rescaled (tw, th, Graphics::highResamplingQuality);
    }
    repaint();
}

void FreakoutAudioProcessorEditor::resized()
{
    // anti-gel : jamais de setSize ici ; les limites du constrainer suffisent
    auto sq = [] (Point<float> c, float half) { return Rectangle<float> (c.x - half, c.y - half, half * 2.0f, half * 2.0f); };
    range.setBounds (scaled (sq (layout::skirtC, rangeHalf)));
    range.unitScale = (float) range.getWidth() / (rangeHalf * 2.0f);
    typeKnob.setBounds (scaled (sq (layout::typeC, typeHalf)));
    typeKnob.unitScale = (float) typeKnob.getWidth() / (typeHalf * 2.0f);
    auto tog = [] (Point<float> c)
    {
        return Rectangle<float> (c.x - layout::toggleHalfW, c.y - layout::toggleAbove, layout::toggleHalfW * 2.0f, layout::toggleAbove + layout::toggleBelow);
    };
    momentarySw.setBounds (scaled (tog (layout::momentaryPos)));
    drySw.setBounds (scaled (tog (layout::dryPos)));
    foot.setBounds (scaled (sq (layout::footPos, layout::footR)));
    resizeSettle = 4;
}

//==============================================================================
// Anti-gel v2 (Studio Wiga) :
//  1. moteur de rendu logiciel force sur Windows, applique a chaque nouvelle fenetre hote et reverifie toutes les secondes ;
//  2. aucun calcul lourd dans paint() : caches reconstruits par le timer (apres stabilisation de la taille / changement d'echelle) ;
//  3. chien de garde : redessin demande non servi depuis 1,5 s -> moteur de rendu reapplique, fenetre redessinee ;
//  4. cote audio : aucun appel a l'hote ni allocation depuis le thread audio (voir PluginProcessor).
void FreakoutAudioProcessorEditor::useSoftwareRenderer()
{
    auto* peer = getPeer();
    if (peer != lastPeer) { lastPeer = peer; repaint(); }
   #if JUCE_WINDOWS
    if (peer != nullptr)
    {
        int sw = peer->getAvailableRenderingEngines().indexOf ("Software Renderer");
        if (sw < 0) sw = 0;
        if (peer->getCurrentRenderingEngine() != sw)
            peer->setCurrentRenderingEngine (sw);
    }
   #endif
}

void FreakoutAudioProcessorEditor::parentHierarchyChanged() { useSoftwareRenderer(); }
void FreakoutAudioProcessorEditor::visibilityChanged()      { useSoftwareRenderer(); if (isShowing()) repaint(); }

void FreakoutAudioProcessorEditor::askRepaint (Rectangle<int> r)
{
    if (repaintAskedMs <= 0.0) repaintAskedMs = Time::getMillisecondCounterHiRes();
    repaint (r);
}

//==============================================================================
static String noteName (float hz)
{
    if (hz <= 0.0f) return {};
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = roundToInt (69.0f + 12.0f * std::log2 (hz / 440.0f));
    return String (n[((midi % 12) + 12) % 12]) + String (midi / 12 - 1);
}

String FreakoutAudioProcessorEditor::buildStatusLine() const
{
    const int i = lastChanged;
    if (i < 0) return {};
    const float v = proc.raw (valueIds[i]);
    switch (i)
    {
        case 0: return "GAIN  " + String (roundToInt (v * 10.0f)) + " %";
        case 1: return "ONSET  " + String (roundToInt (v * 10.0f)) + " %";
        case 2: return "TYPE  " + FreakoutAudioProcessor::typeNames()[jlimit (0, 6, roundToInt (v))];
        case 3: return v > 0.5f ? "MOMENTARY  ON" : "MOMENTARY  OFF";
        case 4: return v > 0.5f ? "DRY  ON" : "DRY  OFF";
        case 5: return v > 0.5f ? "EFFECT  ON" : "BYPASS";
        default: return "SENSITIVITY  " + FreakoutAudioProcessor::sensNames()[jlimit (0, 2, roundToInt (v))].toUpperCase();
    }
}

// au repos : ce qui larsene en ce moment (harmonique, frequence, note)
String FreakoutAudioProcessorEditor::buildInfoLine() const
{
    const auto type = FreakoutAudioProcessor::typeNames()[jlimit (0, 6, roundToInt (proc.raw ("type")))];
    if (proc.raw ("active") < 0.5f) return proc.raw ("momentary") > 0.5f ? "HOLD THE FOOTSWITCH" : "BYPASS";
    const float hz = proc.fbHz.load();
    if (proc.meter.load() < 0.03f || hz <= 0.0f) return "TYPE  " + type;
    const int h = proc.fbHarmonic.load();
    const String hs = h < 0 ? String ("SUB") : ("H" + String (h));
    return hs + "   " + String (roundToInt (hz)) + " Hz   " + noteName (hz);
}

String FreakoutAudioProcessorEditor::buildPresetLine() const
{
    const int cp = proc.currentPreset.load();
    String s;
    if (cp >= 0 && cp < FreakoutAudioProcessor::getNumFactoryPresets())
        s << "P" << (cp + 1) << "  " << FreakoutAudioProcessor::getFactoryPreset (cp).name;
    else if (cp > 100) s << "User " << (cp - 100);
    else               s << "Edited";
    return s;
}

//==============================================================================
void FreakoutAudioProcessorEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes();
    if (getPeer() != lastPeer || now - lastRendererCheckMs > 1000.0) { lastRendererCheckMs = now; useSoftwareRenderer(); }

    if (resizeSettle > 0 && --resizeSettle == 0) rebuildCaches();
    else if (resizeSettle == 0 && now - lastScaleCheckMs > 250.0)
    {
        lastScaleCheckMs = now;
        const float sc = physicalScale();
        if (std::abs (sc - cacheScale) > 0.01f) rebuildCaches (sc);
    }

    if (repaintAskedMs > 0.0 && isShowing() && now - repaintAskedMs > 1500.0 && lastPaintMs < repaintAskedMs)
    {
        repaintAskedMs = 0.0;
        lastPeer = nullptr;
        useSoftwareRenderer();
        repaint();
    }

    momentarySw.tick (now);
    drySw.tick (now);
    if (typeKnob.tick()) askRepaint (scaled (Rectangle<float> (layout::typeC.x - typeHalf, layout::typeC.y - typeHalf, typeHalf * 2.0f, typeHalf * 2.0f)));

    // ligne d'etat : apparait quand un reglage bouge, reste 2,5 s puis s'efface
    int changed = -1, numChanged = 0;
    for (int i = 0; i < numValueIds; ++i)
    {
        const float v = proc.raw (valueIds[i]);
        if (std::abs (v - lastValues[i]) > 1.0e-4f) { lastValues[i] = v; changed = i; ++numChanged; }
    }
    const int pcount = proc.presetCounter.load();
    if (pcount != lastPresetCounter) { lastPresetCounter = pcount; numChanged = 0; }
    if (numChanged == 1 && changed != 5) { lastChanged = changed; showStatus(); }
    if (range.dragging >= 0) { lastChanged = range.dragging; showStatus(); }
    const int mc = proc.midiCounter.load();
    if (mc != lastMidiCount) { lastMidiCount = mc; midiFlash = 8; }
    if (midiFlash > 0) --midiFlash;

    const float target = now < statusUntil ? 1.0f : 0.0f;
    statusAlpha += (target - statusAlpha) * (target > statusAlpha ? 0.45f : 0.12f);
    if (statusAlpha < 0.005f) statusAlpha = 0.0f;
    if (statusAlpha > 0.995f) statusAlpha = 1.0f;

    // echelle de LED : LED centrale = effet actif, puis paires de l'interieur vers l'exterieur selon la montee du larsen
    const bool on = proc.raw ("active") > 0.5f;
    ledLevel += ((on ? 1.0f : 0.0f) - ledLevel) * (on ? 0.45f : 0.28f);
    if (ledLevel < 0.003f) ledLevel = 0.0f;
    if (ledLevel > 0.997f) ledLevel = 1.0f;
    const float m = on ? proc.meter.load() : 0.0f;
    meterShown += (m - meterShown) * (m > meterShown ? 0.5f : 0.25f);
    if (std::abs (meterShown - m) < 0.002f) meterShown = m;

    bool ledsChanged = false;
    for (int i = 0; i < 9; ++i)
    {
        const int pair = std::abs (i - 4);
        const float b = pair == 0 ? ledLevel : ledLevel * jlimit (0.0f, 1.0f, meterShown * 4.0f - (float) (pair - 1));
        ledB[i] = b;
        if (std::abs (b - drawnLedB[i]) > 0.004f) { drawnLedB[i] = b; ledsChanged = true; }
    }
    const float glowKey = ledLevel * 10.0f + meterShown;
    if (ledsChanged || std::abs (glowKey - drawnGlow) > 0.004f)
    {
        drawnGlow = glowKey;
        askRepaint (scaled (layout::glowRect));
    }

    const auto l1 = buildStatusLine(), info = buildInfoLine(), pl = buildPresetLine();
    const bool textChanged = l1 != lastLine1 || info != lastInfo;
    lastLine1 = l1; lastInfo = info;
    if (std::abs (statusAlpha - drawnStatus) > 0.003f || textChanged)
    {
        drawnStatus = statusAlpha;
        askRepaint (scaled (statusRect.expanded (4.0f)));
    }
    if (pl != lastPreset || hoverZone != drawnHover || midiFlash > 0)
    {
        lastPreset = pl; drawnHover = hoverZone;
        askRepaint (scaled (presetRect.expanded (3.0f)));
    }
}

//==============================================================================
void FreakoutAudioProcessorEditor::drawLeds (Graphics& g)
{
    // la facade recoit la lumiere des LED : calques precalcules (fond x eclairement x couleur), 5 etats cumulatifs
    const float s4 = jlimit (0.0f, 4.0f, meterShown * 4.0f);
    const int k = jmin (3, (int) s4);
    const float frac = s4 - (float) k;
    if (ledLevel > 0.003f && glowScaled[0].isValid())
    {
        g.setOpacity (ledLevel);
        g.drawImage (glowScaled[k], layout::glowRect);
        if (frac > 0.003f || s4 >= 4.0f)
        {
            g.setOpacity (ledLevel * (s4 >= 4.0f ? 1.0f : frac));
            g.drawImage (glowScaled[s4 >= 4.0f ? 4 : k + 1], layout::glowRect);
        }
        g.setOpacity (1.0f);
    }

}

void FreakoutAudioProcessorEditor::drawDisplay (Graphics& g)
{
    // ligne du haut : reglage qui vient de bouger (fondu), sinon ce qui larsene en ce moment ; couleur ambre du visuel
    const float a = statusAlpha;
    const auto sr = statusRect.toNearestInt();
    auto glowText = [&] (const String& t, Font f, float alpha)
    {
        g.setFont (f);
        g.setColour (lcd.withAlpha (0.16f * alpha));                    // leger halo de l'afficheur
        for (auto d : { Point<int> (-1, 0), Point<int> (1, 0), Point<int> (0, -1), Point<int> (0, 1) })
            g.drawFittedText (t, sr.translated (d.x, d.y), Justification::centred, 1, 0.8f);
        g.setColour (lcd.withAlpha (alpha));
        g.drawFittedText (t, sr, Justification::centred, 1, 0.8f);
    };
    if (a < 0.999f)
        glowText (lastInfo, Font (FontOptions (boldFace).withHeight (28.0f)).withExtraKerningFactor (0.05f), 0.95f * (1.0f - a));
    if (a > 0.0f)
        glowText (lastLine1, Font (FontOptions (boldFace).withHeight (28.0f)).withExtraKerningFactor (0.05f), a);

    // barre de presets (son cadre fait partie du fond) : fleches et nom
    const auto r = presetRect;
    if (hoverZone != 0)
    {
        g.setColour (Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (r.reduced (1.5f), 4.0f);
    }
    const float aw = 30.0f;
    auto arrow = [&] (Rectangle<float> ar, bool left, bool hot)
    {
        Path p;
        const auto c = ar.getCentre();
        const float s = 4.6f;
        if (left) p.addTriangle (c.x + s * 0.6f, c.y - s, c.x + s * 0.6f, c.y + s, c.x - s * 0.8f, c.y);
        else      p.addTriangle (c.x - s * 0.6f, c.y - s, c.x - s * 0.6f, c.y + s, c.x + s * 0.8f, c.y);
        g.setColour (inkWhite.withAlpha (hot ? 1.0f : 0.72f));
        g.fillPath (p);
    };
    arrow (r.withWidth (aw), true, hoverZone == 1);
    arrow (r.withTrimmedLeft (r.getWidth() - aw), false, hoverZone == 3);
    g.setFont (Font (FontOptions (labelFace).withHeight (20.0f)).withExtraKerningFactor (0.03f));
    g.setColour ((midiFlash > 0 ? Colour (0xffffb08a) : inkWhite).withAlpha (hoverZone == 2 ? 1.0f : 0.9f));
    g.drawFittedText (lastPreset, r.reduced (aw, 0.0f).toNearestInt(), Justification::centred, 1, 0.75f);
}

void FreakoutAudioProcessorEditor::drawKnobs (Graphics& g)
{
    // ONSET : repere sur le dessus de la jupe, cache par le chapeau quand il passe derriere (perspective du visuel)
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (skirtVisible());
        paintPointer (g, layout::skirtC, layout::skirtRx, layout::skirtRy, knobAngle (range.valueOf (1)), 0.71f, 0.975f, 3.3f, false);
    }
    // GAIN : repere sur la face superieure du chapeau
    paintPointer (g, layout::capC, layout::capRx, layout::capRy, knobAngle (range.valueOf (0)), 0.20f, 0.92f, 3.6f, false);
    // TYPE : repere a bouts arrondis (crans de 45 degres, rotation animee)
    paintPointer (g, layout::typeC, layout::typeRx, layout::typeRy, typeKnob.getShownAngle(), 0.19f, 0.91f, 4.1f, true);
}

void FreakoutAudioProcessorEditor::drawFootswitch (Graphics& g)
{
    if (! foot.isPressed() || ! fsDownS.isValid()) return;
    // bouton enfonce dans son ecrou (sprite tire du visuel) + disque assombri (comportement Echoflex)
    g.drawImage (fsDownS, layout::footRect);
    // le dessus du bouton est descendu de ~2,5 unites dans le sprite : le disque le suit, bord adouci
    const auto c = layout::footPos.translated (0.0f, 3.0f * layout::unitPerPhotoPx);
    const float r = layout::footR * 0.54f;
    ColourGradient shade (Colours::black.withAlpha (0.25f), c.x, c.y, Colours::black.withAlpha (0.0f), c.x + r, c.y, true);
    shade.addColour (0.75, Colours::black.withAlpha (0.22f));
    g.setGradientFill (shade);
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
}

void FreakoutAudioProcessorEditor::paint (Graphics& g)
{
    lastPaintMs = Time::getMillisecondCounterHiRes();
    repaintAskedMs = 0.0;
    g.fillAll (Colours::black);
    const Image& bg = bgScaled.isValid() ? bgScaled : background;
    g.setImageResamplingQuality (&bg == &background ? Graphics::highResamplingQuality : Graphics::lowResamplingQuality);
    if (bg.isValid()) g.drawImage (bg, getLocalBounds().toFloat());

    const float s = (float) getHeight() / (float) baseH;
    g.addTransform (AffineTransform::scale (s));
    drawLeds (g);
    drawKnobs (g);
    drawFootswitch (g);
    drawDisplay (g);
}

//==============================================================================
// souris : barre de presets (fleches = precedent / suivant, nom = menu), clic droit = menu
//==============================================================================
static int presetZoneAt (Point<float> p)
{
    if (! presetRect.contains (p)) return 0;
    if (p.x < presetRect.getX() + 30.0f) return 1;
    if (p.x > presetRect.getRight() - 30.0f) return 3;
    return 2;
}

void FreakoutAudioProcessorEditor::mouseMove (const MouseEvent& e)
{
    const float s = (float) getHeight() / (float) baseH;
    hoverZone = presetZoneAt (e.position / s);
    setMouseCursor (hoverZone != 0 ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
}

void FreakoutAudioProcessorEditor::mouseExit (const MouseEvent&) { hoverZone = 0; }

void FreakoutAudioProcessorEditor::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showContextMenu(); return; }
    const float s = (float) getHeight() / (float) baseH;
    switch (presetZoneAt (e.position / s))
    {
        case 1: stepPreset (-1); break;
        case 2: showPresetMenu(); break;
        case 3: stepPreset (1); break;
        default: break;
    }
}

void FreakoutAudioProcessorEditor::stepPreset (int delta)
{
    const int n = FreakoutAudioProcessor::getNumFactoryPresets();
    int cp = proc.currentPreset.load();
    if (cp < 0 || cp >= n) cp = delta > 0 ? -1 : 0;
    proc.applyFactoryPreset ((cp + delta + n) % n);
}

void FreakoutAudioProcessorEditor::showPresetMenu()
{
    PopupMenu m;
    m.addSectionHeader ("Factory presets");
    for (int i = 0; i < FreakoutAudioProcessor::getNumFactoryPresets(); ++i)
        m.addItem (100 + i, String (i + 1) + ". " + FreakoutAudioProcessor::getFactoryPreset (i).name, true, proc.currentPreset.load() == i);
    m.addSeparator();
    PopupMenu load, save;
    for (int slot = 1; slot <= 9; ++slot)
    {
        const bool exists = proc.hasUserPreset (slot);
        load.addItem (200 + slot, "User " + String (slot) + (exists ? "" : "  (empty)"), exists, proc.currentPreset.load() == 100 + slot);
        save.addItem (300 + slot, "User " + String (slot) + (exists ? "  (overwrite)" : ""));
    }
    m.addSubMenu ("Load user preset", load);
    m.addSubMenu ("Save user preset", save);
    m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (scaled (presetRect))),
        [safe = Component::SafePointer<FreakoutAudioProcessorEditor> (this)] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& pr = safe->proc;
            if      (r >= 100 && r < 200) pr.applyFactoryPreset (r - 100);
            else if (r > 200 && r < 300)  pr.loadUserPreset (r - 200);
            else if (r > 300 && r < 400)  pr.saveUserPreset (r - 300);
        });
}

void FreakoutAudioProcessorEditor::showContextMenu()
{
    PopupMenu menu, sizes;
    const int defaultW = roundToInt ((float) defaultH * (float) baseW / (float) baseH);
    sizes.addItem (1, "Default (" + String (defaultW) + " x " + String (defaultH) + ")", true, getHeight() == defaultH);
    sizes.addItem (2, "75 %",  true, getHeight() == roundToInt (baseH * 0.75f));
    sizes.addItem (3, "100 %", true, getHeight() == baseH);
    menu.addSubMenu ("Window size", sizes);
    menu.addSeparator();

    PopupMenu factory;
    for (int i = 0; i < FreakoutAudioProcessor::getNumFactoryPresets(); ++i)
        factory.addItem (100 + i, String (i + 1) + ". " + FreakoutAudioProcessor::getFactoryPreset (i).name, true, proc.currentPreset.load() == i);
    menu.addSubMenu ("Factory presets", factory);
    PopupMenu load, save;
    for (int slot = 1; slot <= 9; ++slot)
    {
        const bool exists = proc.hasUserPreset (slot);
        load.addItem (200 + slot, "User " + String (slot) + (exists ? "" : "  (empty)"), exists, proc.currentPreset.load() == 100 + slot);
        save.addItem (300 + slot, "User " + String (slot) + (exists ? "  (overwrite)" : ""));
    }
    menu.addSubMenu ("Load user preset", load);
    menu.addSubMenu ("Save user preset", save);
    menu.addSeparator();

    PopupMenu sens;
    const int sNow = jlimit (0, 2, roundToInt (proc.raw ("sensitivity")));
    const char* sensHelp[3] = { "Low (hot pickups, noisy room)", "Normal (default)", "High (low output, clean playing)" };
    for (int i = 0; i < 3; ++i) sens.addItem (50 + i, sensHelp[i], true, sNow == i);
    menu.addSubMenu ("Trigger sensitivity", sens);

    PopupMenu settings;
    settings.addItem (41, "MIDI footswitch latching (127 = on / 0 = off)", true, proc.raw ("midiFsMode") < 0.5f);
    settings.addItem (42, "MIDI footswitch toggles on each press", true, proc.raw ("midiFsMode") > 0.5f);
    settings.addSeparator();
    settings.addItem (43, "(MOMENTARY ON: the MIDI footswitch works while held)", false);
    menu.addSubMenu ("Settings", settings);

    PopupMenu midi;
    midi.addItem (900, "CC 20  Gain", false);
    midi.addItem (901, "CC 21  Onset", false);
    midi.addItem (902, "CC 22  Type (7 zones)", false);
    midi.addItem (903, "CC 23  Momentary (64+ on)", false);
    midi.addItem (904, "CC 24  Dry (64+ on)", false);
    midi.addItem (905, "CC 25  Footswitch", false);
    midi.addItem (906, "CC 52-53  fine (14-bit) for CC 20-21", false);
    midi.addItem (907, "Program Change 1-13  Factory presets", false);
    menu.addSubMenu ("MIDI map", midi);
    menu.addSeparator();
    menu.addItem (999, "FreakOut feedback v" + String (JucePlugin_VersionString) + " - STUDIO WIGA", false);

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this),
        [safe = Component::SafePointer<FreakoutAudioProcessorEditor> (this)] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& ed = *safe;
            auto& pr = ed.proc;
            if      (r == 1) ed.setScaledHeight (defaultH);
            else if (r == 2) ed.setScaledHeight (roundToInt (baseH * 0.75f));
            else if (r == 3) ed.setScaledHeight (baseH);
            else if (r == 41) pr.setParamValue ("midiFsMode", 0.0f);
            else if (r == 42) pr.setParamValue ("midiFsMode", 1.0f);
            else if (r >= 50 && r <= 52) pr.setParamValue ("sensitivity", (float) (r - 50));
            else if (r >= 100 && r < 200) pr.applyFactoryPreset (r - 100);
            else if (r > 200 && r < 300)  pr.loadUserPreset (r - 200);
            else if (r > 300 && r < 400)  pr.saveUserPreset (r - 300);
        });
}
