// Poorboys Checkpoint Manager: the map grid as the game's debug vertices (see WorldLinesEmit.h)
#include "WorldLinesEmit.h"
#include <algorithm>
#include <cmath>

namespace worldlines
{
	namespace
	{
		// the jump-spot marks: the old helper's pen widths (pixels at 1080 lines) and the Present path's colours
		struct SpotStyle
		{
			float width;
			uint8_t r, g, b;
		};
		constexpr SpotStyle kSpotStyles[3] = {
			{ 2.0f, 60, 255, 110 },     // green: landing edges
			{ 1.0f, 255, 140, 20 },     // yellow group: fast-drop seams, orange
			{ 1.5f, 255, 90, 230 },     // cyan group: take-off marks, magenta (kTakeOffRgb)
		};

		const std::vector<Seg>& spotGroup(const Scene& s, uint32_t group)
		{
			return group == 0 ? s.green : group == 1 ? s.yellow : s.cyan;
		}

		// the game's debug drawer takes linear colours: degamma as its own debug lines do
		uint8_t degamma(float c)
		{
			c = std::clamp(c, 0.f, 1.f);
			return (uint8_t)std::lround(std::pow(c, 2.2f) * 255.f);
		}

		struct Frame
		{
			EmitCamera cam;
			V3 planes[4];           // side planes (a point v relative to the camera is inside when dot(v, n) >= 0)
			float nearAllow = 1.f;  // ends up to this far behind the camera still count as in front
		};

		// as Jagged's build_frustum: per axis the wider of the two field-of-view readings, with a generous margin
		void buildFrustum(Frame& f)
		{
			float th = std::tan(f.cam.hfov * 0.5f), tv = std::tan(f.cam.vfov * 0.5f);
			if (tv * f.cam.aspect > th) th = tv * f.cam.aspect;
			if (th / f.cam.aspect > tv) tv = th / f.cam.aspect;
			th = th * 1.35f + 0.1f;
			tv = tv * 1.35f + 0.1f;
			const V3 fw = f.cam.fwd, l = f.cam.left, u = f.cam.up;
			f.planes[0] = fw * th - l;
			f.planes[1] = fw * th + l;
			f.planes[2] = fw * tv - u;
			f.planes[3] = fw * tv + u;
		}

		bool culled(const Frame& f, V3 a, V3 b, float maxSq)
		{
			const V3 va = a - f.cam.pos, vb = b - f.cam.pos;
			if (maxSq > 0.f && dot(va, va) > maxSq && dot(vb, vb) > maxSq) return true;
			if (dot(va, f.cam.fwd) < -f.nearAllow && dot(vb, f.cam.fwd) < -f.nearAllow) return true;
			for (const V3& n : f.planes)
				if (dot(va, n) < 0.f && dot(vb, n) < 0.f) return true;
			return false;
		}

		V3 pull(const Frame& f, V3 p, float frac) { return frac > 0.f ? p + (f.cam.pos - p) * frac : p; }

		bool normalise(V3& v)
		{
			const float n = length(v);
			if (n < 1e-8f) return false;
			v = v * (1.f / n);
			return true;
		}

		void push(std::vector<DebugVertex>& out, V3 p, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
		{
			out.push_back({ p.x, p.y, p.z, b, g, r, a });
		}

		// one edge: a one-pixel line, or a camera-facing ribbon `widthPx` wide (two triangles) sized per end so it keeps
		// its width on screen along its length
		void edge(const Frame& f, V3 a, V3 b, float widthPx, float pullFrac, uint8_t r, uint8_t g, uint8_t bl, uint8_t al, Emitted& out)
		{
			a = pull(f, a, pullFrac);
			b = pull(f, b, pullFrac);
			if (widthPx <= 1.05f)
			{
				push(out.lines, a, r, g, bl, al);
				push(out.lines, b, r, g, bl, al);
				return;
			}
			const V3 dir = b - a, va = a - f.cam.pos, vb = b - f.cam.pos;
			V3 pa = cross(dir, va), pb = cross(dir, vb);
			if (!normalise(pa) || !normalise(pb))
			{
				push(out.lines, a, r, g, bl, al);
				push(out.lines, b, r, g, bl, al);
				return;
			}
			const float k = std::tan(f.cam.vfov * 0.5f) / (std::max)(f.cam.heightPx, 1.f);    // world units per pixel per unit distance
			const float ha = widthPx * length(va) * k, hb = widthPx * length(vb) * k;
			const V3 a0 = a + pa * ha, a1 = a - pa * ha, b0 = b + pb * hb, b1 = b - pb * hb;
			for (V3 p : { a0, b0, a1, b0, b1, a1 }) push(out.tris, p, r, g, bl, al);
		}
	}

	void EdgeCells::build(const Scene& s)
	{
		mItems.clear(), mStarts.clear(), mCellItems.clear(), mBig.clear();
		std::vector<std::pair<V3, V3>> ends;
		for (int k = 0; k < kEdgeKinds; k++)
			for (size_t i = 0; i + 1 < s.edgeIndex[k].size(); i += 2)
			{
				const uint32_t a = s.edgeIndex[k][i], b = s.edgeIndex[k][i + 1];
				if (a >= s.verts.size() || b >= s.verts.size()) continue;
				mItems.push_back({ a, b, 0, (uint8_t)k });
				ends.push_back({ s.verts[a], s.verts[b] });
			}
		for (uint32_t g = 0; g < 3; g++)
		{
			const std::vector<Seg>& group = spotGroup(s, g);
			for (uint32_t i = 0; i < group.size(); i++)
			{
				mItems.push_back({ 0xFFFFFFFF, 0xFFFFFFFF, g << 24 | i, 255 });
				ends.push_back({ group[i].a, group[i].b });
			}
		}
		if (ends.empty())
		{
			mDims[0] = mDims[1] = mDims[2] = 0;
			return;
		}
		V3 lo = ends[0].first, hi = lo;
		for (const auto& e : ends)
			for (V3 p : { e.first, e.second })
			{
				lo = { (std::min)(lo.x, p.x), (std::min)(lo.y, p.y), (std::min)(lo.z, p.z) };
				hi = { (std::max)(hi.x, p.x), (std::max)(hi.y, p.y), (std::max)(hi.z, p.z) };
			}
		mOrigin = lo;
		const float span[3] = { hi.x - lo.x, hi.y - lo.y, hi.z - lo.z };
		for (int k = 0; k < 3; k++) mDims[k] = std::clamp((int)(span[k] / kCell) + 1, 1, 4096);
		const size_t cells = (size_t)mDims[0] * mDims[1] * mDims[2];
		if (cells > 64u * 1024 * 1024)            // a broken file: everything in the always-list
		{
			mDims[0] = mDims[1] = mDims[2] = 0;
			for (uint32_t i = 0; i < mItems.size(); i++) mBig.push_back(i);
			return;
		}
		auto range = [&](const std::pair<V3, V3>& e, int r0[3], int r1[3]) {
			const float a[3] = { e.first.x, e.first.y, e.first.z }, b[3] = { e.second.x, e.second.y, e.second.z }, o[3] = { mOrigin.x, mOrigin.y, mOrigin.z };
			int n = 1;
			for (int k = 0; k < 3; k++)
			{
				r0[k] = std::clamp((int)std::floor(((std::min)(a[k], b[k]) - o[k]) / kCell), 0, mDims[k] - 1);
				r1[k] = std::clamp((int)std::floor(((std::max)(a[k], b[k]) - o[k]) / kCell), 0, mDims[k] - 1);
				n *= r1[k] - r0[k] + 1;
			}
			return n;
			};
		// counting pass, then fill (compressed rows)
		std::vector<uint32_t> count(cells + 1, 0);
		for (uint32_t i = 0; i < mItems.size(); i++)
		{
			int r0[3], r1[3];
			if (range(ends[i], r0, r1) > 64) continue;
			for (int z = r0[2]; z <= r1[2]; z++)
				for (int y = r0[1]; y <= r1[1]; y++)
					for (int x = r0[0]; x <= r1[0]; x++) count[((size_t)z * mDims[1] + y) * mDims[0] + x]++;
		}
		mStarts.assign(cells + 1, 0);
		for (size_t c = 0; c < cells; c++) mStarts[c + 1] = mStarts[c] + count[c];
		mCellItems.assign(mStarts[cells], 0);
		std::vector<uint32_t> fill(mStarts.begin(), mStarts.end() - 1);
		for (uint32_t i = 0; i < mItems.size(); i++)
		{
			int r0[3], r1[3];
			if (range(ends[i], r0, r1) > 64)
			{
				mBig.push_back(i);
				continue;
			}
			for (int z = r0[2]; z <= r1[2]; z++)
				for (int y = r0[1]; y <= r1[1]; y++)
					for (int x = r0[0]; x <= r1[0]; x++) mCellItems[fill[((size_t)z * mDims[1] + y) * mDims[0] + x]++] = i;
		}
	}

	void emit(const Scene& scene, const EdgeCells& cells, const EmitCamera& cam, const EmitOptions& opt, EmitScratch& scratch, Emitted& out)
	{
		out.clear();
		Frame f;
		f.cam = cam;
		buildFrustum(f);
		const auto& items = cells.all();
		if (scratch.stamp.size() != items.size()) scratch.stamp.assign(items.size(), 0), scratch.frame = 0;
		if (++scratch.frame == 0) std::fill(scratch.stamp.begin(), scratch.stamp.end(), 0), scratch.frame = 1;
		const uint32_t frame = scratch.frame;
		const float scale = (std::max)(cam.heightPx, 1.f) / 1080.f;
		const float gridWidth = std::clamp(opt.gridWidth, 0.25f, 8.f) * scale;
		const uint8_t gridAlpha = (uint8_t)std::lround(std::clamp(opt.gridOpacity, 0.f, 1.f) * 255.f);
		const uint8_t colours[kEdgeKinds][3] = {
			{ degamma(opt.structure[0]), degamma(opt.structure[1]), degamma(opt.structure[2]) },
			{ degamma(opt.instance[0]), degamma(opt.instance[1]), degamma(opt.instance[2]) },
		};
		uint8_t spotColours[3][3];
		for (int g = 0; g < 3; g++)
			spotColours[g][0] = degamma(kSpotStyles[g].r / 255.f), spotColours[g][1] = degamma(kSpotStyles[g].g / 255.f),
			spotColours[g][2] = degamma(kSpotStyles[g].b / 255.f);
		const float gridSq = opt.maxDistance > 0.f ? opt.maxDistance * opt.maxDistance : 0.f;
		const float spotSq = opt.spotsDistance * opt.spotsDistance;

		auto take = [&](uint32_t i) {
			if (scratch.stamp[i] == frame) return;
			scratch.stamp[i] = frame;
			const EdgeCells::Item& it = items[i];
			out.considered++;
			if (it.kind == 255)
			{
				if (!opt.spots) return;
				const uint32_t g = it.piece >> 24, n = it.piece & 0xFFFFFF;
				const std::vector<Seg>& group = spotGroup(scene, g);
				if (n >= group.size()) return;
				const Seg& s = group[n];
				if (culled(f, s.a, s.b, spotSq))
				{
					out.culled++;
					return;
				}
				edge(f, s.a, s.b, kSpotStyles[g].width * scale, opt.depthPull, spotColours[g][0], spotColours[g][1], spotColours[g][2], 255, out);
				return;
			}
			if (!opt.grid) return;
			const V3 a = scene.verts[it.a], b = scene.verts[it.b];
			if (culled(f, a, b, gridSq))
			{
				out.culled++;
				return;
			}
			edge(f, a, b, gridWidth, opt.depthPull, colours[it.kind][0], colours[it.kind][1], colours[it.kind][2], gridAlpha, out);
			};

		if (opt.grid && opt.maxDistance <= 0.f)
		{
			for (uint32_t i = 0; i < items.size(); i++) take(i);
			return;
		}
		const float radius = (std::max)(opt.grid ? opt.maxDistance : 0.f, opt.spots ? opt.spotsDistance : 0.f);
		cells.forNear(cam.pos, radius, take);
	}
}
