#pragma once

// Gravação de notas tocadas ao vivo (MIDI ou teclado do PC) na pattern atual.
// Roda na thread da UI; os eventos chegam com a posição absoluta do transporte.

#include <map>
#include <vector>

#include "core/Song.h"

namespace lynx
{
struct RecordedEvent
{
    double tick = 0.0; // posição absoluta do transporte (pode passar do fim do loop)
    int channel = 0;
    int key = 60;
    float velocity = 0.8f;
    bool on = true;
};

// Onde uma posição absoluta do transporte cai dentro da pattern atual: no modo pattern,
// a posição dentro do loop; no modo song, dentro do clip da pattern atual que estiver
// tocando ali (ou, sem clip, como se fosse o loop da pattern).
std::int64_t patternPositionForTick (const Song& song, double tick, std::int64_t loopLength);

class NoteRecorder
{
public:
    // Começa uma tomada. `loopLength` = tamanho do loop da pattern no início da tomada
    // (não muda enquanto as notas gravadas aumentam a pattern).
    void begin (std::int64_t loopLength);
    bool isActive() const noexcept { return active; }

    // Entrega um evento; quando é o "off" de uma nota presa, a nota pronta vai para finished().
    void add (const Song& song, const RecordedEvent& e);

    // Fecha as notas ainda presas na posição `tick` (parou de tocar) e encerra a tomada.
    void end (const Song& song, double tick);

    // Notas completas desde a última chamada.
    std::vector<Note> takeFinished();

private:
    struct Held
    {
        double tick;
        float velocity;
    };

    void finish (const Song& song, int channel, int key, const Held& held, double endTick);

    bool active = false;
    std::int64_t loop = 0;
    std::map<std::pair<int, int>, Held> held; // (canal, tecla)
    std::vector<Note> finished;
};
} // namespace lynx
