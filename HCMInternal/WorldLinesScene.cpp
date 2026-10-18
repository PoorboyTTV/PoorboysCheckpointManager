// Poorboys Checkpoint Manager: the bounce lines of one map (see WorldLinesScene.h). Built without the precompiled
// header (HCMInternal.vcxproj: NotUsing) so tools\worldlines_test compiles the same file.
#include "WorldLinesScene.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace worldlines
{
	float length(V3 a) { return std::sqrt(dot(a, a)); }

	// ---- BoxGrid ------------------------------------------------------------------------------------------------

	namespace
	{
		constexpr int64_t kCellLimit = (int64_t)1 << 21;      // cells per axis (2M; 4M world units at 2 a cell)
		constexpr int64_t kBigBox = 4096;                     // a box over this many cells goes in the always-tested list
		constexpr float kTriCell = 1.f;                       // triangle grid cell, world units
		constexpr float kPiece = 1.f;                         // rays are tested in pieces this long (each piece: a few cells)

		uint64_t cellKey(int64_t x, int64_t y, int64_t z) { return (uint64_t)x | ((uint64_t)y << 21) | ((uint64_t)z << 42); }
	}

	bool BoxGrid::cellOf(V3 p, int64_t out[3]) const
	{
		const float c[3] = { (p.x - mOrigin.x) / mCell, (p.y - mOrigin.y) / mCell, (p.z - mOrigin.z) / mCell };
		for (int k = 0; k < 3; k++)
		{
			if (!std::isfinite(c[k])) return false;
			out[k] = (int64_t)std::floor(c[k]);
		}
		return true;
	}

	void BoxGrid::build(const std::vector<V3>& lo, const std::vector<V3>& hi, float cell)
	{
		mCell = cell;
		mCellIndex.clear();
		mStarts.clear();
		mItems.clear();
		mBig.clear();
		if (lo.empty()) return;
		mOrigin = lo[0];
		for (const V3& v : lo)
		{
			mOrigin.x = std::min(mOrigin.x, v.x);
			mOrigin.y = std::min(mOrigin.y, v.y);
			mOrigin.z = std::min(mOrigin.z, v.z);
		}
		std::vector<std::pair<uint64_t, uint32_t>> pairs;
		pairs.reserve(lo.size() * 2);
		for (uint32_t i = 0; i < (uint32_t)lo.size(); i++)
		{
			int64_t a[3], b[3];
			if (!cellOf(lo[i], a) || !cellOf(hi[i], b)) continue;
			bool inside = true;
			int64_t span = 1;
			for (int k = 0; k < 3; k++)
			{
				if (a[k] < 0 || b[k] >= kCellLimit || b[k] < a[k]) inside = false;
				span *= std::max<int64_t>(1, b[k] - a[k] + 1);
			}
			if (!inside || span > kBigBox)
			{
				mBig.push_back(i);
				continue;
			}
			for (int64_t x = a[0]; x <= b[0]; x++)
				for (int64_t y = a[1]; y <= b[1]; y++)
					for (int64_t z = a[2]; z <= b[2]; z++)
						pairs.emplace_back(cellKey(x, y, z), i);
		}
		std::sort(pairs.begin(), pairs.end());
		mItems.reserve(pairs.size());
		mCellIndex.reserve(pairs.size() / 2 + 1);
		for (size_t j = 0; j < pairs.size(); j++)
		{
			if (j == 0 || pairs[j].first != pairs[j - 1].first)
			{
				mCellIndex.emplace(pairs[j].first, (uint32_t)mStarts.size());
				mStarts.push_back((uint32_t)mItems.size());
			}
			mItems.push_back(pairs[j].second);
		}
		mStarts.push_back((uint32_t)mItems.size());
	}

	void BoxGrid::query(V3 lo, V3 hi, std::vector<uint32_t>& out) const
	{
		out.insert(out.end(), mBig.begin(), mBig.end());
		if (mCellIndex.empty()) return;
		int64_t a[3], b[3];
		if (!cellOf(lo, a) || !cellOf(hi, b)) return;
		for (int k = 0; k < 3; k++)
		{
			a[k] = std::max<int64_t>(a[k], 0);
			b[k] = std::min<int64_t>(b[k], kCellLimit - 1);
			if (b[k] < a[k]) return;
		}
		if ((b[0] - a[0] + 1) * (b[1] - a[1] + 1) * (b[2] - a[2] + 1) > 1'000'000) return;     // never asked for; a guard
		for (int64_t x = a[0]; x <= b[0]; x++)
			for (int64_t y = a[1]; y <= b[1]; y++)
				for (int64_t z = a[2]; z <= b[2]; z++)
				{
					auto it = mCellIndex.find(cellKey(x, y, z));
					if (it == mCellIndex.end()) continue;
					out.insert(out.end(), mItems.begin() + mStarts[it->second], mItems.begin() + mStarts[it->second + 1]);
				}
	}

	// ---- a small JSON reader for our own spot files ---------------------------------------------------------------

	namespace
	{
		struct J
		{
			enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
			double num = 0;
			bool boolean = false;
			std::string str;
			std::vector<J> arr;
			std::vector<std::pair<std::string, J>> obj;

			const J* get(const char* key) const
			{
				for (auto& kv : obj)
					if (kv.first == key) return &kv.second;
				return nullptr;
			}
			double number(const char* key, double fallback) const
			{
				const J* v = get(key);
				return v && v->type == Num ? v->num : fallback;
			}
		};

		class JsonReader
		{
			const char* p;
			const char* end;
			int depth = 0;

			[[noreturn]] void fail(const char* what) { throw std::runtime_error(std::string("spot file: ") + what); }
			void skip()
			{
				while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
			}
			bool literal(const char* word)
			{
				const size_t n = std::strlen(word);
				if ((size_t)(end - p) < n || std::memcmp(p, word, n) != 0) return false;
				p += n;
				return true;
			}
			std::string string()
			{
				if (p >= end || *p != '"') fail("expected a string");
				p++;
				std::string out;
				while (p < end && *p != '"')
				{
					if (*p == '\\' && p + 1 < end)
					{
						p++;
						switch (*p)
						{
						case 'n': out += '\n'; break;
						case 't': out += '\t'; break;
						case 'r': out += '\r'; break;
						case 'b': out += '\b'; break;
						case 'f': out += '\f'; break;
						case 'u': out += '?'; p += std::min<ptrdiff_t>(4, end - p - 1); break;     // not used by our files
						default: out += *p; break;
						}
						p++;
					}
					else out += *p++;
				}
				if (p >= end) fail("unfinished string");
				p++;
				return out;
			}
		public:
			JsonReader(const char* begin, const char* finish) : p(begin), end(finish) {}

			J value()
			{
				if (++depth > 64) fail("nested too deep");
				skip();
				if (p >= end) fail("ended early");
				J v;
				if (*p == '{')
				{
					v.type = J::Obj;
					p++;
					skip();
					if (p < end && *p == '}') p++;
					else
						for (;;)
						{
							skip();
							std::string key = string();
							skip();
							if (p >= end || *p != ':') fail("expected ':'");
							p++;
							v.obj.emplace_back(std::move(key), value());
							skip();
							if (p < end && *p == ',') { p++; continue; }
							if (p < end && *p == '}') { p++; break; }
							fail("expected ',' or '}'");
						}
				}
				else if (*p == '[')
				{
					v.type = J::Arr;
					p++;
					skip();
					if (p < end && *p == ']') p++;
					else
						for (;;)
						{
							v.arr.push_back(value());
							skip();
							if (p < end && *p == ',') { p++; continue; }
							if (p < end && *p == ']') { p++; break; }
							fail("expected ',' or ']'");
						}
				}
				else if (*p == '"')
				{
					v.type = J::Str;
					v.str = string();
				}
				else if (literal("true")) { v.type = J::Bool; v.boolean = true; }
				else if (literal("false")) { v.type = J::Bool; v.boolean = false; }
				else if (literal("null")) v.type = J::Null;
				else if (literal("NaN")) { v.type = J::Num; v.num = NAN; }
				else if (literal("Infinity")) { v.type = J::Num; v.num = INFINITY; }
				else if (literal("-Infinity")) { v.type = J::Num; v.num = -INFINITY; }
				else
				{
					v.type = J::Num;
					auto r = std::from_chars(p, end, v.num);       // never the C locale's decimal point
					if (r.ec != std::errc()) fail("bad number");
					p = r.ptr;
				}
				depth--;
				return v;
			}
		};

		J readJson(const std::filesystem::path& path)
		{
			std::ifstream f(path, std::ios::binary);
			if (!f) throw std::runtime_error("cannot open " + path.filename().string());
			std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			JsonReader r(text.data(), text.data() + text.size());
			return r.value();
		}

		V3 v3(const J* j)
		{
			if (!j || j->type != J::Arr || j->arr.size() < 2) throw std::runtime_error("spot file: expected a point");
			V3 v{ (float)j->arr[0].num, (float)j->arr[1].num, 0.f };
			if (j->arr.size() >= 3) v.z = (float)j->arr[2].num;
			return v;
		}

		// ---- the world file -------------------------------------------------------------------------------------

		struct Reader
		{
			const std::vector<char>& d;
			size_t p = 0;
			void need(size_t n) const
			{
				if (p + n > d.size()) throw std::runtime_error("world file is still being written (ends early)");
			}
			template <typename T> T get()
			{
				need(sizeof(T));
				T v;
				std::memcpy(&v, d.data() + p, sizeof(T));
				p += sizeof(T);
				return v;
			}
		};

		struct QKey
		{
			int64_t x, y, z;
			bool operator==(const QKey& o) const { return x == o.x && y == o.y && z == o.z; }
		};
		struct QHash
		{
			size_t operator()(const QKey& k) const
			{
				uint64_t h = (uint64_t)k.x * 0x9E3779B97F4A7C15ull;
				h ^= (uint64_t)k.y * 0xC2B2AE3D27D4EB4Full + (h << 6) + (h >> 2);
				h ^= (uint64_t)k.z * 0x165667B19E3779F9ull + (h << 6) + (h >> 2);
				return (size_t)h;
			}
		};
	}

	std::shared_ptr<Scene> Scene::load(const std::filesystem::path& worldFile, const std::filesystem::path& spotsDir, const std::string& mapName)
	{
		std::error_code ec;
		const auto time = std::filesystem::last_write_time(worldFile, ec);
		if (ec) throw std::runtime_error("no " + worldFile.filename().string() + " yet (the mod writes it once you are in the game)");
		std::vector<char> bytes;
		{
			std::ifstream f(worldFile, std::ios::binary);
			if (!f) throw std::runtime_error("cannot open " + worldFile.filename().string());
			bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		}
		Reader r{ bytes };
		r.need(52);
		if (std::memcmp(bytes.data(), "H3WD", 4) != 0) throw std::runtime_error("not a world file of the mod");
		r.p = 4;
		const uint32_t version = r.get<uint32_t>();
		const uint32_t npoly = r.get<uint32_t>();
		r.get<uint32_t>();                      // BSP count
		r.get<uint32_t>();                      // instance count
		if (version != 1 && version != 2) throw std::runtime_error("world file of an unknown version");
		if (npoly > 50'000'000) throw std::runtime_error("world file is damaged");
		auto scene = std::make_shared<Scene>();
		scene->worldFile = worldFile;
		scene->worldTime = time;
		scene->polygons = npoly;
		{
			char name[33] = {};
			std::memcpy(name, bytes.data() + 20, 32);
			scene->map = name[0] ? std::string(name) : mapName;
			std::transform(scene->map.begin(), scene->map.end(), scene->map.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		}
		r.p = 52;

		// polygons: their own copy of each vertex; the same point (to 1e-4) is the same vertex (h3wd.DumpGeom)
		std::vector<V3>& verts = scene->verts;
		std::unordered_map<QKey, uint32_t, QHash> ids;
		std::vector<uint32_t> ringStart, ring;                 // welded vertex ids per polygon
		std::vector<uint8_t> kind;                            // EdgeKind of each polygon
		ringStart.reserve(npoly + 1);
		kind.reserve(npoly);
		for (uint32_t i = 0; i < npoly; i++)
		{
			r.get<uint32_t>();                  // surface reference
			kind.push_back((uint8_t)(r.get<int32_t>() >= 0 ? kInstance : kStructure));     // instance (-1 = the structure BSP)
			r.get<uint8_t>();                   // bsp
			r.get<uint8_t>();                   // flags
			r.get<int16_t>();                   // material
			const uint16_t nv = r.get<uint16_t>();
			r.get<uint16_t>();                  // plane
			r.get<float>();                     // normal
			r.get<float>();
			r.get<float>();
			if (version >= 2) r.get<uint32_t>();     // surface index
			ringStart.push_back((uint32_t)ring.size());
			r.need((size_t)nv * 12);
			for (uint16_t k = 0; k < nv; k++)
			{
				V3 v;
				v.x = r.get<float>();
				v.y = r.get<float>();
				v.z = r.get<float>();
				// half to even, as numpy's round (the helpers' welding): llrint in the default rounding mode
				const QKey key{ std::llrint((double)v.x * 1e4), std::llrint((double)v.y * 1e4), std::llrint((double)v.z * 1e4) };
				auto [it, added] = ids.try_emplace(key, (uint32_t)verts.size());
				if (added) verts.push_back(v);
				ring.push_back(it->second);
			}
		}
		ringStart.push_back((uint32_t)ring.size());

		// every polygon edge once (h3wd.DumpGeom._edges: the same two welded vertices = the same edge), drawn in the colour
		// of the first polygon that has it
		struct EdgeRec
		{
			uint32_t lo, hi, face;
		};
		std::vector<EdgeRec> edges;
		edges.reserve(ring.size());
		for (uint32_t f = 0; f < npoly; f++)
		{
			const uint32_t s = ringStart[f], c = ringStart[f + 1] - s;
			if (c < 3) continue;
			for (uint32_t k = 0; k < c; k++)
			{
				const uint32_t a = ring[s + k], b = ring[s + (k + 1) % c];
				if (a != b) edges.push_back({ std::min(a, b), std::max(a, b), f });
			}
		}
		std::sort(edges.begin(), edges.end(), [](const EdgeRec& x, const EdgeRec& y) {
			return x.lo != y.lo ? x.lo < y.lo : x.hi != y.hi ? x.hi < y.hi : x.face < y.face;
			});
		for (size_t s = 0; s < edges.size(); s++)
			if (s == 0 || edges[s].lo != edges[s - 1].lo || edges[s].hi != edges[s - 1].hi)
			{
				auto& list = scene->edgeIndex[kind[edges[s].face]];
				list.push_back(edges[s].lo);
				list.push_back(edges[s].hi);
			}

		// fan triangles: the depth pass, and the line-of-sight test of the labels
		std::vector<V3> lo, hi;
		for (uint32_t f = 0; f < npoly; f++)
		{
			const uint32_t s = ringStart[f], c = ringStart[f + 1] - s;
			for (uint32_t j = 1; j + 1 < c; j++)
			{
				const uint32_t i0 = ring[s], i1 = ring[s + j], i2 = ring[s + j + 1];
				scene->triIndex.insert(scene->triIndex.end(), { i0, i1, i2 });
				const V3 a = verts[i0], b = verts[i1], c2 = verts[i2];
				lo.push_back({ std::min({ a.x, b.x, c2.x }), std::min({ a.y, b.y, c2.y }), std::min({ a.z, b.z, c2.z }) });
				hi.push_back({ std::max({ a.x, b.x, c2.x }), std::max({ a.y, b.y, c2.y }), std::max({ a.z, b.z, c2.z }) });
			}
		}
		scene->mTriGrid.build(lo, hi, kTriCell);

		scene->loadSpots(spotsDir);
		return scene;
	}

	void Scene::loadSpots(const std::filesystem::path& spotsDir)
	{
		std::error_code ec;
		const auto det = spotsDir / ("det_" + map + ".json");
		if (std::filesystem::exists(det, ec))
		{
			const J root = readJson(det);
			const J* hits = root.get("hits");
			std::vector<long long> seen;
			if (hits && hits->type == J::Arr)
				for (const J& h : hits->arr)          // surest first
				{
					if (h.number("big", 0) < 9) continue;
					const long long ei = (long long)h.number("ei", -1);
					if (std::find(seen.begin(), seen.end(), ei) == seen.end())
					{
						seen.push_back(ei);
						green.push_back({ v3(h.get("a")), v3(h.get("b")) });
					}
					const V3 q = v3(h.get("takeoff"));
					bool apart = rings.size() < 60;
					for (const Ring& p : rings)
						if (apart && length(q - p.pos) <= 1.2f) apart = false;
					if (!apart) continue;
					const J* fam = h.get("family");
					const std::string family = fam && fam->type == J::Str ? fam->str : "";
					const J* late = h.get("late");
					Ring ring;
					ring.pos = q;
					ring.dir = v3(h.get("dir"));
					ring.dir.z = 0;
					ring.at = v3(h.get("at") ? h.get("at") : h.get("mid"));
					ring.label = std::to_string(rings.size() + 1) + " " + (family == "forward" ? "HOLD" : family == "letgo" ? "LET GO" : "STRAFE")
						+ (late && late->type == J::Bool && late->boolean ? " +late jump" : "");
					rings.push_back(std::move(ring));
				}
		}
		const auto spots = spotsDir / ("spots_" + map + ".json");
		if (std::filesystem::exists(spots, ec))
		{
			const J root = readJson(spots);
			if (const J* probe = root.get("probe"); probe && probe->type == J::Arr)
				for (const J& s : probe->arr)
					if (s.type == J::Arr && s.arr.size() >= 2) yellow.push_back({ v3(&s.arr[0]), v3(&s.arr[1]) });
			if (const J* list = root.get("spots"); list && list->type == J::Arr)
				for (const J& s : list->arr) yellow.push_back({ v3(s.get("a")), v3(s.get("b")) });
		}
		// a hair off the floor so a line is not buried in it
		for (auto* group : { &green, &yellow })
			for (Seg& s : *group)
			{
				s.a.z += 0.01f;
				s.b.z += 0.01f;
			}
		// take-off marks: a ring of 16 pieces, an arrow, a cross at the landing point
		for (const Ring& p : rings)
		{
			const V3 c = p.pos + V3{ 0, 0, 0.02f };
			V3 prev{};
			for (int i = 0; i <= 16; i++)
			{
				const float ang = 2.f * 3.14159265358979f * i / 16.f;
				const V3 pt = c + V3{ 0.14f * std::cos(ang), 0.14f * std::sin(ang), 0 };
				if (i) cyan.push_back({ prev, pt });
				prev = pt;
			}
			const V3 d = p.dir, side{ -d.y, d.x, 0 };
			const V3 tip = c + d * 0.75f;
			cyan.push_back({ c, tip });
			cyan.push_back({ tip, tip - d * 0.16f + side * 0.09f });
			cyan.push_back({ tip, tip - d * 0.16f - side * 0.09f });
			const V3 a = p.at + V3{ 0, 0, 0.02f };
			cyan.push_back({ a - side * 0.10f, a + side * 0.10f });
			cyan.push_back({ a - d * 0.10f, a + d * 0.10f });
			ringMid.push_back(p.pos + V3{ 0, 0, 0.1f });
		}
	}

	// ---- ray tests (Moller-Trumbore on the fan triangles, two-sided, as h3map.World.segment_hits) -------------------

	bool Scene::pieceHit(V3 a, V3 b, bool any, float& tBest, std::vector<uint32_t>& scratch) const
	{
		scratch.clear();
		const V3 lo{ std::min(a.x, b.x) - 1e-6f, std::min(a.y, b.y) - 1e-6f, std::min(a.z, b.z) - 1e-6f };
		const V3 hi{ std::max(a.x, b.x) + 1e-6f, std::max(a.y, b.y) + 1e-6f, std::max(a.z, b.z) + 1e-6f };
		mTriGrid.query(lo, hi, scratch);
		if (scratch.empty()) return false;
		std::sort(scratch.begin(), scratch.end());
		scratch.erase(std::unique(scratch.begin(), scratch.end()), scratch.end());
		const double eps = 1e-9;
		const double dx = (double)b.x - a.x, dy = (double)b.y - a.y, dz = (double)b.z - a.z;
		bool hit = false;
		for (uint32_t i : scratch)
		{
			const V3 t[3] = { verts[triIndex[3 * (size_t)i]], verts[triIndex[3 * (size_t)i + 1]], verts[triIndex[3 * (size_t)i + 2]] };
			const double e1x = (double)t[1].x - t[0].x, e1y = (double)t[1].y - t[0].y, e1z = (double)t[1].z - t[0].z;
			const double e2x = (double)t[2].x - t[0].x, e2y = (double)t[2].y - t[0].y, e2z = (double)t[2].z - t[0].z;
			const double hx = dy * e2z - dz * e2y, hy = dz * e2x - dx * e2z, hz = dx * e2y - dy * e2x;
			const double det = e1x * hx + e1y * hy + e1z * hz;
			if (std::abs(det) <= 1e-14) continue;
			const double inv = 1.0 / det;
			const double sx = (double)a.x - t[0].x, sy = (double)a.y - t[0].y, sz = (double)a.z - t[0].z;
			const double u = (sx * hx + sy * hy + sz * hz) * inv;
			if (u < -eps) continue;
			const double qx = sy * e1z - sz * e1y, qy = sz * e1x - sx * e1z, qz = sx * e1y - sy * e1x;
			const double v = (qx * dx + qy * dy + qz * dz) * inv;
			if (v < -eps || u + v > 1 + eps) continue;
			const double tt = (e2x * qx + e2y * qy + e2z * qz) * inv;
			if (tt < 0 || tt > 1) continue;
			if (!hit || tt < tBest) tBest = (float)tt;
			hit = true;
			if (any) return true;
		}
		return hit;
	}

	bool Scene::firstHit(V3 p, V3 q, float& t) const
	{
		thread_local std::vector<uint32_t> scratch;
		const V3 d = q - p;
		const int n = std::max(1, (int)std::ceil(length(d) / kPiece));
		for (int i = 0; i < n; i++)
		{
			float tp = 0;
			if (pieceHit(p + d * ((float)i / n), p + d * ((float)(i + 1) / n), false, tp, scratch))
			{
				t = (i + tp) / n;
				return true;
			}
		}
		return false;
	}

	bool Scene::lineOfSight(V3 p, V3 q) const
	{
		thread_local std::vector<uint32_t> scratch;
		const V3 d = (q - p) * 0.985f;
		const int n = std::max(1, (int)std::ceil(length(d) / kPiece));
		for (int i = 0; i < n; i++)
		{
			float tp = 0;
			if (pieceHit(p + d * ((float)i / n), p + d * ((float)(i + 1) / n), true, tp, scratch)) return false;
		}
		return true;
	}

	// ---- camera, screen, labels ------------------------------------------------------------------------------------

	bool cameraFromRecord(const uint8_t* raw, Camera& c)
	{
		float v[9];
		std::memcpy(v, raw, sizeof(v));
		float vfov;
		std::memcpy(&vfov, raw + 0x28, 4);
		int16_t b[4];
		std::memcpy(b, raw + 0x38, sizeof(b));
		for (float x : v)
			if (!std::isfinite(x)) return false;
		c.pos = { v[0], v[1], v[2] };
		c.fwd = { v[3], v[4], v[5] };
		c.up = { v[6], v[7], v[8] };
		if (std::abs(length(c.fwd) - 1) > 1e-2f || std::abs(length(c.up) - 1) > 1e-2f || !(vfov > 0.2f && vfov < 2.9f)) return false;
		const int w = b[3] - b[1], h = b[2] - b[0];
		const float aspect = w > 0 && h > 0 ? (float)w / h : 16.f / 9.f;
		c.right = cross(c.fwd, c.up);
		c.ty = std::tan(vfov / 2);
		c.tx = c.ty * aspect;
		return true;
	}

	bool toScreen(const Camera& cam, V3 p, float W, float H, float& x, float& y)
	{
		const V3 d = p - cam.pos;
		const float z = dot(d, cam.fwd);
		if (z <= kNear) return false;
		x = W / 2 * (1 + dot(d, cam.right) / (z * cam.tx));
		y = H / 2 * (1 - dot(d, cam.up) / (z * cam.ty));
		return true;
	}

	std::vector<Label> visibleLabels(const Scene& sc, const Camera& cam)
	{
		std::vector<Label> out;
		for (size_t i = 0; i < sc.rings.size(); i++)
		{
			const V3 at = sc.rings[i].pos + V3{ 0, 0, 0.35f };
			const V3 d = at - cam.pos;
			if (dot(d, cam.fwd) <= kNear || length(d) >= 14.f) continue;
			if (sc.lineOfSight(cam.pos, sc.ringMid[i] + V3{ 0, 0, 0.05f })) out.push_back({ at, sc.rings[i].label });
		}
		return out;
	}
}
