#include "../engine/page.hpp"
#include <fstream>
#include <iostream>
int main(int argc, char **argv) {
  using namespace pv;
  try {
    std::string input, url = "https://local.test/", out;
    int width = 1000, height = 700;
    std::map<std::string, Resource> resources;
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      auto next = [&]() -> std::string {
        if (i + 1 >= argc)
          throw std::runtime_error("missing argument");
        return argv[++i];
      };
      if (a == "--width")
        width = std::stoi(next());
      else if (a == "--height")
        height = std::stoi(next());
      else if (a == "--url")
        url = next();
      else if (a == "--out")
        out = next();
      else if (a == "--resource") {
        auto spec = next();
        auto eq = spec.find('=');
        if (eq == std::string::npos)
          throw std::runtime_error("resource expects URL=file");
        Resource r;
        r.url = spec.substr(0, eq);
        auto path = spec.substr(eq + 1);
        r.text = read_text_file(path);
        r.bytes.assign(r.text.begin(), r.text.end());
        resources[r.url] = r;
      } else if (a == "--help") {
        std::cout << "PsychoVault owned engine renderer\n--input FILE --url "
                     "URL --width N --height N --out PNG --resource URL=FILE\n";
        return 0;
      } else if (a == "--input")
        input = next();
      else
        input = a;
    }
    if (input.empty())
      throw std::runtime_error("input HTML file required");
    auto parsed = parse_url(url);
    if (!parsed)
      throw std::runtime_error("invalid URL");
    Page page;
    page.load(*parsed, read_text_file(input));
    for (auto &[u, r] : resources)
      page.resource(r);
    for (int pass = 0; pass < 4; ++pass) {
      auto pending = page.requests;
      page.requests.clear();
      for (auto &u : pending) {
        auto it = resources.find(u);
        if (it != resources.end())
          page.resource(it->second);
        else {
          Resource r;
          r.url = u;
          r.ok = false;
          r.error = "resource not supplied to headless renderer";
          page.resource(r);
        }
      }
    }
    auto frame = page.render(width, height, 0);
    Canvas canvas(width, height, 0xffffffff);
    std::vector<PaintCommand> commands;
    for (auto &j : frame.at("paint").array) {
      PaintCommand c;
      c.kind = PaintCommand::Kind(int(j.num("kind")));
      c.rect = {j.num("x"), j.num("y"), j.num("w"), j.num("h")};
      c.color = uint32_t(j.num("color"));
      c.text = j.str("text");
      c.size = j.num("size");
      c.thickness = j.num("thickness");
      c.bold = j.flag("bold");
      c.node = int(j.num("node"));
      c.url = j.str("url");
      commands.push_back(c);
    }
    paint_commands(canvas, commands, 0, resources);
    if (!out.empty()) {
      auto png = encode_png(width, height, canvas.pixels);
      std::ofstream f(out, std::ios::binary);
      f.write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()));
      if (!f)
        throw std::runtime_error("cannot write PNG");
    }
    std::cout << frame.dump() << "\n";
    for (auto &d : page.diagnostics)
      std::cerr << d.subsystem << ": " << d.message << "\n";
    return 0;
  } catch (std::exception &e) {
    std::cerr << "pv-engine: " << e.what() << "\n";
    return 1;
  }
}
