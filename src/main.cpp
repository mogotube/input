// Predictive Input Buffer - mod de Geode para Geometry Dash
//
// Idea: cada click (press/release) no se aplica al instante. Se guarda en un
// buffer y se aplica N ticks de fisica despues (1 tick = 1/240 s = 0.25 frames
// a 60 fps). N sale de cuanto tiempo paso desde el ultimo tick de fisica, con
// un tope ("Limit") segun la etapa activa. F6 cambia de etapa.
//
// Etapas:
//   1: 2 ticks  all | 2 ticks wave
//   2: 4 ticks  all | 4 ticks wave
//   3: 8 ticks  all | 8 ticks wave
//   4: 16 ticks all | 8 ticks wave
//   5: OFF (input crudo)

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>

#include <chrono>
#include <deque>

using namespace geode::prelude;

namespace {
	constexpr double TPS = 240.0;
	constexpr double TICK_SECONDS = 1.0 / TPS;
	constexpr double TICKS_PER_FRAME_60 = 4.0; // 240 / 60

	struct Stage {
		int ticksAll;
		int ticksWave;
	};

	// indice 0 = etapa 1
	constexpr Stage STAGES[5] = {
		{2, 2},
		{4, 4},
		{8, 8},
		{16, 8},
		{0, 0}, // OFF
	};

	struct BufferedInput {
		bool down;
		int button;
		bool isPlayer1;
		long long dueStep;
	};

	using Clock = std::chrono::steady_clock;

	int g_stage = 4;            // 0..4 (arranca en etapa 5 = OFF, input crudo)
	long long g_step = 0;       // contador de ticks de fisica del intento actual
	std::deque<BufferedInput> g_queue;
	Clock::time_point g_lastStepTime = Clock::now();
	bool g_flushing = false;    // evita re-bufferear lo que nosotros mismos aplicamos

	bool bufferEnabled() {
		return g_stage < 4;
	}

	void printStage() {
		int s = g_stage + 1;
		if (!bufferEnabled()) {
			log::info("[InputBuffer] F6 Cycle -> STAGE {}: OFF (100% Raw Input)", s);
			return;
		}
		auto const& st = STAGES[g_stage];
		double waveFrames = st.ticksWave / TICKS_PER_FRAME_60;
		if (st.ticksAll == st.ticksWave) {
			log::info(
				"[InputBuffer] F6 Cycle -> STAGE {}: {} Ticks All | {} Ticks Wave ({:.1f}f @ 60fps)",
				s, st.ticksAll, st.ticksWave, waveFrames
			);
		} else {
			log::info(
				"[InputBuffer] F6 Cycle -> STAGE {}: {} Ticks All | {} Ticks Wave ({:.1f}f / {:.1f}f @ 60fps)",
				s, st.ticksAll, st.ticksWave, st.ticksAll / TICKS_PER_FRAME_60, waveFrames
			);
		}
	}

	void resetBuffer() {
		g_queue.clear();
		g_step = 0;
		g_lastStepTime = Clock::now();
	}
}

class $modify(IBGameLayer, GJBaseGameLayer) {
	// Cada vez que el juego procesa un paso de fisica: avanzamos el contador y
	// aplicamos los inputs que ya vencieron.
	void processCommands(float dt) {
		GJBaseGameLayer::processCommands(dt);

		g_step++;
		g_lastStepTime = Clock::now();

		if (g_queue.empty()) return;

		g_flushing = true;
		while (!g_queue.empty() && g_queue.front().dueStep <= g_step) {
			auto in = g_queue.front();
			g_queue.pop_front();
			GJBaseGameLayer::handleButton(in.down, in.button, in.isPlayer1);
		}
		g_flushing = false;
	}

	void handleButton(bool down, int button, bool isPlayer1) {
		// Fuera de PlayLayer (editor) o con el buffer apagado: input crudo.
		if (g_flushing || !bufferEnabled() || !PlayLayer::get()) {
			GJBaseGameLayer::handleButton(down, button, isPlayer1);
			return;
		}

		auto const& st = STAGES[g_stage];
		bool wave = false;
		auto* player = isPlayer1 ? m_player1 : m_player2;
		if (player) wave = player->m_isDart;

		int limit = wave ? st.ticksWave : st.ticksAll;

		// ticks "perdidos" desde el ultimo paso de fisica, medidos en tiempo real
		double elapsed = std::chrono::duration<double>(Clock::now() - g_lastStepTime).count();
		int ticks = static_cast<int>(std::lround(elapsed / TICK_SECONDS));
		if (ticks < 1) ticks = 1;
		if (ticks > limit) ticks = limit;

		// mantener el orden: un evento nunca vence antes que uno anterior
		long long due = g_step + ticks;
		if (!g_queue.empty() && g_queue.back().dueStep > due) {
			due = g_queue.back().dueStep;
		}

		g_queue.push_back({down, button, isPlayer1, due});

		if (down && Mod::get()->getSettingValue<bool>("log-presses")) {
			log::info(
				"[InputBuffer] *** SAVED +{} TICKS ({:.2f} frames @ 60fps) PRESS! (Frame0: {} -> Delayed: {}) [Limit: {}] ***",
				ticks, ticks / TICKS_PER_FRAME_60, g_step / 4, due, limit
			);
		}
	}
};

class $modify(IBPlayLayer, PlayLayer) {
	void resetLevel() {
		PlayLayer::resetLevel();
		resetBuffer();
	}

	void onQuit() {
		resetBuffer();
		PlayLayer::onQuit();
	}
};

class $modify(IBKeyboard, CCKeyboardDispatcher) {
	// OJO: en algunas versiones de Geode/GD esta funcion no lleva el ultimo
	// parametro (double timestamp). Si no compila, borralo.
	bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp) {
		if (down && !repeat && key == KEY_F6) {
			g_stage = (g_stage + 1) % 5;
			g_queue.clear();
			printStage();
		}
		return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
	}
};

$on_mod(Loaded) {
	log::info("[InputBuffer] Predictive Input Buffer cargado");
	printStage();
}
