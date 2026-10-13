#pragma once

#include <memory>
#include <string>

#include <juce_audio_processors/juce_audio_processors.h>

namespace lynx
{
// Plugins em processo separado ("sandbox"), opcional: cada plugin roda num LYNX.exe filho
// (`--plugin-host`). Áudio, MIDI e parâmetros passam por memória compartilhada a cada bloco; estado e
// janela, por mensagens. Se o filho travar ou fechar, o plugin fica mudo e o LYNX continua.
// Só no Windows; nos outros sistemas `createSandboxedPlugin` falha e o plugin roda no processo.

constexpr const char* pluginSandboxId = "lynx-plugin-host";

// Ligado/desligado (arquivo sandbox.txt em %APPDATA%\LYNX; padrão: desligado).
bool pluginSandboxEnabled();
void setPluginSandboxEnabled (bool enabled);

// Abre o plugin num processo filho já preparado (taxa, bloco, estado em base64). nullptr + `error` se
// não der (filho não abriu, plugin não carregou, tempo esgotado).
std::unique_ptr<juce::AudioPluginInstance> createSandboxedPlugin (const juce::PluginDescription& description, double sampleRate,
                                                                  int blockSize, const std::string& stateBase64, juce::String& error);

// Verdadeiro se o plugin é um proxy de sandbox e o processo dele morreu (travou, fechou ou não respondeu).
bool isSandboxedPluginDead (const juce::AudioPluginInstance& plugin);

// Modo filho: se a linha de comando é de um processo de sandbox, conecta ao pai e retorna true (o app
// não deve abrir janela nem áudio). O processo fecha sozinho quando o pai some.
bool runPluginSandboxWorker (const juce::String& commandLine);
void shutdownPluginSandboxWorker();

// Testes (--sandboxtest): sem caixa de aviso quando um plugin morre; e um pedido que faz o filho
// travar na próxima chamada de áudio (para conferir o tempo-limite).
void setPluginSandboxAlerts (bool show);
void hangSandboxedPluginForTest (juce::AudioPluginInstance& plugin);
} // namespace lynx
