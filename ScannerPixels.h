#pragma once
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>

namespace scanner {
std::filesystem::path UserDataDirectory();
std::wstring NewId();
class PixelBuffer {
	struct State {
		mutable std::vector<uint8_t> bytes;
		mutable std::filesystem::path backing;
		size_t length = 0;
		mutable bool loaded = true;
		~State() { if (!backing.empty()) { std::error_code ec; std::filesystem::remove(backing, ec); } }
	};
	std::shared_ptr<State> state = std::make_shared<State>();
	const std::vector<uint8_t>& Read() const {
		static const std::vector<uint8_t> empty;
		if (!state) return empty;
		if (!state->loaded) {
			std::ifstream input(state->backing, std::ios::binary);
			std::vector<uint8_t> data(state->length);
			input.read(reinterpret_cast<char*>(data.data()), data.size());
			if (!input || input.peek() != EOF) throw std::runtime_error("Ausgelagerte Bilddaten fehlen oder sind beschaedigt.");
			state->bytes = std::move(data); state->loaded = true;
		}
		return state->bytes;
	}
	std::vector<uint8_t>& Write() {
		if (!state) state = std::make_shared<State>();
		if (state.use_count() != 1) {
			auto next = std::make_shared<State>(); next->bytes = Read(); next->length = next->bytes.size(); state = std::move(next);
		} else {
			Read();
			if (!state->backing.empty()) { std::error_code ec; std::filesystem::remove(state->backing, ec); state->backing.clear(); }
		}
		return state->bytes;
	}
public:
	size_t size() const { return state ? state->length : 0; }
	bool empty() const { return size() == 0; }
	size_t ResidentBytes() const { return state && state->loaded ? state->bytes.size() : 0; }
	void resize(size_t size, uint8_t value = 0) { auto& bytes = Write(); bytes.resize(size, value); state->length = size; }
	void pop_back() { auto& bytes = Write(); bytes.pop_back(); state->length = bytes.size(); }
	void clear() { state = std::make_shared<State>(); }
	uint8_t* data() { return Write().data(); }
	const uint8_t* data() const { return Read().data(); }
	auto begin() { return Write().begin(); }
	auto end() { return Write().end(); }
	auto begin() const { return Read().begin(); }
	auto end() const { return Read().end(); }
	uint8_t& operator[](size_t index) { return Write()[index]; }
	const uint8_t& operator[](size_t index) const { return Read()[index]; }
	void Spill() const {
		if (empty() || !state->backing.empty()) return;
		auto folder = UserDataDirectory() / L"ImageCache"; std::filesystem::create_directories(folder);
		auto path = folder / (NewId() + L".pixels");
		try {
			std::ofstream output(path, std::ios::binary); const auto& bytes = Read();
			output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); output.close();
			if (!output) throw std::runtime_error("Bildcache kann nicht auf Datentraeger gesichert werden.");
			state->backing = std::move(path);
		} catch (...) { std::error_code ec; std::filesystem::remove(path, ec); throw; }
	}
	void ReleaseMemory() const { if (state && !state->backing.empty()) { std::vector<uint8_t>().swap(state->bytes); state->loaded = false; } }
	friend bool operator==(const PixelBuffer& a, const PixelBuffer& b) { return a.state == b.state || a.Read() == b.Read(); }
	friend bool operator==(const PixelBuffer& a, const std::vector<uint8_t>& b) { return a.Read() == b; }
	friend bool operator==(const std::vector<uint8_t>& a, const PixelBuffer& b) { return a == b.Read(); }
};
struct PixelReadScope {
	const PixelBuffer& pixels;
	~PixelReadScope() { pixels.ReleaseMemory(); }
};
}
