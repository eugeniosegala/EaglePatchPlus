#include <stdio.h>
#include <limits>
#include <map>
#include <string>
#include <fstream>
#include "../EaglePatch/src/ac2_ui_movie.h"
#ifdef _WIN32
#include "../EaglePatch/src/ac2_ui_scale.cpp"
#endif

using namespace ac2_ui::movie;
static unsigned failures = 0;
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "Line %u: %s\n", __LINE__, #expr); ++failures; } } while (0)
static void Length(Bytes& b) { for (unsigned i = 0; i < 4; ++i) b[4 + i] = (unsigned char)(b.size() >> (8 * i)); }
static Bytes Movie()
{
	Bytes b = {'U','E','F',8,0,0,0,0};
	// RECT: 0..25600 by 0..14400 twips, 25 FPS, one frame.
	const unsigned char header[] = {0x80,0,3,0x20,0,0,1,0xc2,0,0,0x19,1,0};
	b.insert(b.end(), header, header + sizeof(header));
	Put16(b, (69 << 6) | 4); Put32(b, 0);
	Put16(b, (12 << 6) | 1); b.push_back(0); // Existing first-frame actions.
	Put16(b, 1 << 6); Put16(b, 0); Length(b); return b;
}
struct Value
{
	std::string text; float number = 0; int object = 0;
	Value() = default;
	explicit Value(float n) : number(n) {}
	explicit Value(const char* s) : text(s) {}
};
// Independent interpreter for the AVM1 instructions emitted by the patch.
static bool Execute(const Bytes& code, std::map<std::string, float>& root, const char* targetName = nullptr,
	std::vector<std::map<std::string,float>>* clips = nullptr)
{
	std::vector<Value> stack;
	std::map<std::string,Value> locals;
	auto pop = [&]() { Value v = stack.back(); stack.pop_back(); return v; };
	for (size_t pc = 0; pc < code.size();)
	{
		const unsigned op = code[pc++]; if (!op) return stack.empty();
		size_t end = pc;
		if (op >= 0x80) { if (pc + 2 > code.size()) return false; end = pc + 2 + U16(&code[pc]); pc += 2; if (end > code.size()) return false; }
		if (op == 0x96)
		{
			if (pc == end) return false;
			const unsigned kind = code[pc++];
			if (kind == 0)
			{
				size_t start = pc; while (pc < end && code[pc]) ++pc;
				if (pc + 1 != end) return false;
				stack.push_back(Value(reinterpret_cast<const char*>(&code[start])));
			}
			else if (kind == 1 && end - pc == 4) { float n; memcpy(&n, &code[pc], 4); stack.push_back(Value(n)); }
			else return false;
			pc = end;
		}
		else if (op == 0x1c)
		{
			if (stack.empty()) return false; auto name = pop().text;
			if (name == "_root" || (targetName && (name == "target" || name == targetName))) { Value v; v.object = 1; stack.push_back(v); }
			else if (locals.count(name)) stack.push_back(locals[name]); else return false;
		}
		else if (op == 0x4e)
		{
			if (stack.size() < 2) return false; Value key = pop(), obj = pop(), v; if (!obj.object) return false;
			if (obj.object == 1 && clips && key.text == "MessageDisplay") v.object = 2;
			else if (obj.object == 2 && clips) v.object = 3;
			else if (obj.object == 3 && clips)
			{
				if (key.text == "length") v.number = float(clips->size());
				else { if (key.number < 0 || key.number >= clips->size()) return false; v.object = 4 + int(key.number); }
			}
			else if (obj.object >= 4 && clips) v.number = (*clips)[obj.object-4][key.text];
			else v = targetName && key.text == "_name" ? Value(targetName) : Value(root[key.text]);
			stack.push_back(v);
		}
		else if (op == 0x4f)
		{
			if (stack.size() < 3) return false; Value v = pop(), key = pop(), obj = pop(); if (!obj.object) return false;
			if (obj.object >= 4 && clips) (*clips)[obj.object-4][key.text] = v.number; else root[key.text] = v.number;
		}
		else if (op == 0x0c || op == 0x0a)
		{
			if (stack.size() < 2) return false; float b = pop().number, a = pop().number;
			stack.push_back(Value(op == 0x0c ? a * b : a + b));
		}
		else if (op == 0x9d || op == 0x99)
		{
			if (end - pc != 2 || (op == 0x9d && stack.empty())) return false;
			const int offset = (int16_t)U16(&code[pc]); pc = end;
			if (op == 0x99 || pop().number != 0) { const int dest = int(pc) + offset; if (dest < 0 || dest > int(code.size())) return false; pc = size_t(dest); }
		}
		else if (op == 0x3c || op == 0x1d) { if (stack.size() < 2) return false; Value v=pop(),key=pop(); locals[key.text]=v; }
		else if (op == 0x50) { if (stack.empty()) return false; stack.push_back(Value(pop().number + 1)); }
		else if (op == 0x48) { if (stack.size() < 2) return false; float b=pop().number,a=pop().number; stack.push_back(Value(float(a<b))); }
		else if (op == 0x49) { if (stack.size() < 2) return false; Value b = pop(), a = pop(); stack.push_back(Value(float(a.text == b.text))); }
		else if (op == 0x12) { if (stack.empty()) return false; stack.push_back(Value(float(pop().number == 0))); }
		else return false;
	}
	return false;
}
static void MessageTests()
{
	Bytes holder; Property(holder,"_x",1,-640,"MessageDisplay_all_mc"); Property(holder,"_y",1,-360,"MessageDisplay_all_mc"); holder.push_back(0);
	std::map<std::string,float> panel={{"_x",96},{"_y",258}};
	CHECK(Execute(holder,panel,"MessageDisplay_all_mc"));
	CHECK(320+0.5f*panel["_x"]==48 && 180+0.5f*panel["_y"]==129);
	const char* arrays[] = {"arrBottomCenterMovieClips","arrRightMovieClips"};
	const float offsets[][2] = {{640,720},{1280,0}};
	for (unsigned i=0; i<2; ++i)
	{
		Bytes actions = MessageArrayActions(arrays[i],0.5f,offsets[i][0],offsets[i][1]);
		std::map<std::string,float> root;
		std::vector<std::map<std::string,float>> clips;
		CHECK(Execute(actions,root,nullptr,&clips)); // No active message.
		clips = {{{"_x",540},{"_y",257}},{{"_x",10},{"_y",20}}};
		CHECK(Execute(actions,root,nullptr,&clips));
		CHECK(clips[0]["_x"] == 540+offsets[i][0] && clips[0]["_y"] == 257+offsets[i][1]);
		CHECK(clips[1]["_x"] == 10+offsets[i][0] && clips[1]["_y"] == 20+offsets[i][1]);
		auto once=clips; CHECK(Execute(actions,root,nullptr,&clips)); CHECK(clips==once);
		clips.push_back({{"_x",30},{"_y",40}}); CHECK(Execute(actions,root,nullptr,&clips));
		CHECK(clips[0]==once[0] && clips[1]==once[1]); CHECK(clips[2]["_x"] == 30+offsets[i][0] && clips[2]["_y"] == 40+offsets[i][1]);
	}
}
static void ComponentTests()
{
	struct Case { const char* name; float x, y; };
	const Case cases[] = {{"HUD_Controls",640,-360},{"HUD_Equipment",-640,360},{"HUD_Money",-640,360},{"HUD_Subtitles",0,360},{"Unrelated",0,0}};
	for (const auto& c : cases)
	{
		Bytes function = ComponentActions(0.5f,1280,720);
		std::map<std::string,float> clip = {{"_x",20},{"_y",40},{"_xscale",100},{"_yscale",100}};
		CHECK(Execute(function,clip,c.name)); CHECK(clip["_x"] == 20+c.x && clip["_y"] == 40+c.y);
		CHECK(clip["_xscale"] == 100 && clip["_yscale"] == 100); // Shared root already provides the scale.
		auto once = clip; CHECK(Execute(function,clip,c.name)); CHECK(clip == once);
	}
	// Verify the listener's embedded function header and body independently.
	Bytes listener = ComponentListener(0.5f,1280,720); size_t p = 0;
	while (p + 3 < listener.size() && !(listener[p] == 0x9b && U16(listener.data()+p+1) == 12)) ++p;
	CHECK(p + 15 < listener.size()); if (p + 15 >= listener.size()) return;
	CHECK(listener[p] == 0x9b); unsigned headerSize = U16(listener.data()+p+1); p += 3;
	CHECK(listener[p] == 0 && U16(listener.data()+p+1) == 1 && !strcmp(reinterpret_cast<const char*>(listener.data()+p+3),"target"));
	CHECK(U16(listener.data()+p+headerSize-2) == ComponentActions(0.5f,1280,720).size());
	CHECK(!memcmp(listener.data()+p+headerSize,ComponentActions(0.5f,1280,720).data(),ComponentActions(0.5f,1280,720).size()));
}
static void TransformTests()
{
	struct Case { const char* name; float x, y; };
	const Case cases[] = {{"TitleMenu",320,180},{"PauseMenu",320,180},{"HudMiniMap",640,360},
		{"HudControlsManager",640,0},{"HudHealthBar",0,0},{"HudMissionState",0,0},{"AFSMessageDisplay",320,180},{"HudMoneyComponent",0,360}};
	for (const auto& c : cases)
	{
		float x, y; Anchor(c.name,x,y); Bytes actions = Actions(0.5f,1280*x,720*y);
		std::map<std::string,float> root = {{"_xscale",100},{"_yscale",80},{"_x",20},{"_y",40}};
		CHECK(Execute(actions,root)); CHECK(root["_xscale"] == 50 && root["_yscale"] == 40);
		CHECK(root["_x"] == c.x+10 && root["_y"] == c.y+20);
		auto once = root; CHECK(Execute(actions,root)); CHECK(root == once);
	}
}
static void ParserTests()
{
	Bytes b = Movie(), out; CHECK(Patch(b.data(),b.size(),0.5f,"TitleMenu",out));
	CHECK(U32(out.data()+4) == out.size()); CHECK(b == Movie());
	CHECK(!Patch(out.data(),out.size(),0.5f,"TitleMenu",b)); CHECK(b.empty()); b = Movie();
	const size_t frame = b.size()-4, added = out.size()-b.size();
	CHECK(U16(out.data()+frame) == ((12<<6)|63)); CHECK(U32(out.data()+frame+2) == added-6);
	Bytes actions(out.begin()+frame+6,out.begin()+frame+added);
	std::map<std::string,float> root = {{"_xscale",100},{"_yscale",100}};
	CHECK(Execute(actions,root)); CHECK(root["_x"] == 320 && root["_y"] == 180);
	CHECK(!memcmp(out.data()+8,b.data()+8,frame-8)); CHECK(!memcmp(out.data()+frame+added,b.data()+frame,b.size()-frame));
	for (size_t n = 0; n < b.size(); ++n) { Bytes truncated(b.begin(),b.begin()+n); if (n>=8) Length(truncated); CHECK(!Patch(truncated.data(),n,0.5f,"TitleMenu",out)); }
	CHECK(!Patch(b.data(),b.size(),0.5f,"HudIconManager",out));
	CHECK(!Patch(b.data(),b.size(),1,"TitleMenu",out)); CHECK(!Patch(nullptr,b.size(),0.5f,"TitleMenu",out));
	CHECK(!ValidScale(0.49f)); CHECK(!ValidScale(1.1f)); CHECK(!ValidScale(std::numeric_limits<float>::quiet_NaN()));
	CHECK(!ValidScale(std::numeric_limits<float>::infinity()));
	Bytes bad=b; bad[0]='C'; CHECK(!Patch(bad.data(),bad.size(),0.5f,"TitleMenu",out));
	bad=b; bad[3]=11; CHECK(!Patch(bad.data(),bad.size(),0.5f,"TitleMenu",out));
	bad=b; bad[8]=0; CHECK(!Patch(bad.data(),bad.size(),0.5f,"TitleMenu",out));
	bad=b; bad[23]=8; CHECK(!Patch(bad.data(),bad.size(),0.5f,"TitleMenu",out)); // AS3.
	bad=b; bad.back()=1; CHECK(!Patch(bad.data(),bad.size(),0.5f,"TitleMenu",out));
	bad=b; bad[frame]=0x41; CHECK(!Patch(bad.data(),bad.size(),0.5f,"TitleMenu",out)); // ShowFrame payload.
	bad=b; bad.insert(bad.end(),3,0); Length(bad); CHECK(Patch(bad.data(),bad.size(),0.5f,"TitleMenu",out));
	bad.back()=1; CHECK(!Patch(bad.data(),bad.size(),0.5f,"TitleMenu",out));
	bad=b; memcpy(bad.data(),"FWS",3); CHECK(Patch(bad.data(),bad.size(),0.5f,"TitleMenu",out));
}
#ifdef _WIN32
static void HookTests()
{
	using namespace ac2_ui::detail;
	CHECK(!ac2_ui::Install(nullptr,0.5f)); CHECK(!ac2_ui::Install(nullptr,1));
	auto* game = static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2190000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
	CHECK(game); if (!game) return;
	auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(game); dos->e_magic=IMAGE_DOS_SIGNATURE; dos->e_lfanew=0x80;
	auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(game+0x80); nt->Signature=IMAGE_NT_SIGNATURE;
	nt->FileHeader.Machine=IMAGE_FILE_MACHINE_I386; nt->FileHeader.TimeDateStamp=0x5925ba62;
	nt->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC; nt->OptionalHeader.SizeOfImage=0x2190000;
	memcpy(game+0x13ac405,"\x8b\x76\x04\x8b\x44\x24\x10\x83\xc6\x50",10);
	memcpy(game+0x13b2dd0,"\x83\xec\x0c\x53\x56\x8d\x44\x24\x0c",9);
	memcpy(game+0x13b28b0,"\x51\x53\x55\x56\x57",5);
	const uintptr_t guards[]={0x13ac405,0x13b2dd0,0x13b28b0};
	for (auto rva:guards) { game[rva]^=1; CHECK(!ac2_ui::Install(reinterpret_cast<HMODULE>(game),0.5f)); game[rva]^=1; CHECK(game[0x13b2dd0]==0x83); }
	// Complete the displaced native prologues with executable status/stack-preservation stubs.
	const unsigned char finishLoad[]={0x8b,0x44,0x24,0x18,0x8b,0x54,0x24,0x24,0x89,0x10,0x5e,0x5b,0x83,0xc4,0x0c,0xc2,0x10,0};
	const unsigned char finishDestroy[]={0x5f,0x5e,0x5d,0x5b,0x59,0xc3};
	memcpy(game+0x13b2dd0+9,finishLoad,sizeof(finishLoad)); memcpy(game+0x13b28b0+5,finishDestroy,sizeof(finishDestroy));
	CHECK(ac2_ui::Install(reinterpret_cast<HMODULE>(game),0.5f)); CHECK(game[0x13b2dd0]==0xe9 && game[0x13b28b0]==0xe9);
	CHECK(!ac2_ui::Install(reinterpret_cast<HMODULE>(game),0.5f));
	uintptr_t state[24]={},item[40]={},table[8]={},type[9]={};
	state[0]=0x1234; state[20]=reinterpret_cast<uintptr_t>(item)+8; item[25]=state[0];
	item[0]=reinterpret_cast<uintptr_t>(table); table[4]=reinterpret_cast<uintptr_t>(game+0x1000);
	const uintptr_t slot=reinterpret_cast<uintptr_t>(game+0x1c6b000),tp=reinterpret_cast<uintptr_t>(type);
	game[0x1000]=0xa1; memcpy(game+0x1001,&slot,4); game[0x1005]=0xc3; memcpy(game+0x1c6b000,&tp,4);
	type[8]=reinterpret_cast<uintptr_t>("TitleMenu");
	Load entry=reinterpret_cast<Load>(game+0x13b2dd0); Destroy dispose=reinterpret_cast<Destroy>(game+0x13b28b0);
	Bytes b=Movie(); int status=-2;
	CHECK(entry(state,&status,b.data(),(uint32_t)b.size(),0)==&status && status==0); CHECK(buffers && retained==buffers->data.size());
	size_t once=retained; CHECK(entry(state,&status,b.data(),(uint32_t)b.size(),0)==&status); CHECK(retained==once);
	CHECK(entry(state,&status,b.data(),(uint32_t)b.size(),uint32_t(-1))==&status && status==-1); CHECK(retained==once*2);
	CHECK(entry(state,&status,b.data(),(uint32_t)b.size(),0)==&status); CHECK(retained==once);
	CHECK(entry(state,&status,nullptr,0,0)==&status); CHECK(!buffers && retained==0);
	entry(state,&status,b.data(),(uint32_t)b.size(),0); dispose(state); CHECK(!buffers && retained==0);
	VirtualFree(reinterpret_cast<void*>(load),0,MEM_RELEASE); VirtualFree(reinterpret_cast<void*>(destroy),0,MEM_RELEASE);
	load=nullptr; destroy=nullptr; DeleteCriticalSection(&storageLock); VirtualFree(game,0,MEM_RELEASE);
}
#endif
int main(int argc, char** argv)
{
	TransformTests(); ParserTests(); ComponentTests(); MessageTests();
#ifdef _WIN32
	HookTests();
#endif
	if (argc>=2) { std::ifstream f(argv[1],std::ios::binary); Bytes b((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>()),out; CHECK(Patch(b.data(),b.size(),0.5f,argc>2?argv[2]:"TitleMenu",out)); printf("Real movie: %u -> %u bytes\n",(unsigned)b.size(),(unsigned)out.size()); }
	if (failures) return 1;
	puts("UI scale tests passed: movie validation, AVM1 transforms, corner anchors, replay guard and native hook lifetime."); return 0;
}
