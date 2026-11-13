#include "../engine/Psycho_page.hpp"
#include <fstream>
#include <iostream>
int main(int argc, char **argv) {
  using namespace pv;
  try {
    std::string input, url = "https://local.test/", out;
    int width = 1000, height = 700;
    double scroll = 0;
    MediaEnvironment environment;
    std::vector<std::string> actions;
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
      else if (a == "--scroll")
        scroll = std::stod(next());
      else if (a == "--dpr")
        environment.device_pixel_ratio = std::stod(next());
      else if (a == "--reduced-motion")
        environment.prefers_reduced_motion = "reduce";
      else if (a == "--action")
        actions.push_back(next());
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
                     "URL --width N --height N --out PNG --resource URL=FILE\n"
                     "--scroll N --dpr N --reduced-motion --action hover:X,Y|click:X,Y|wheel:X,Y,DELTA|key:KEY|text:TEXT|render\n";
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
    page.set_viewport_environment(width, height, environment);
    page.load(*parsed, read_text_file(input));
    for (auto &[u, r] : resources)
      page.resource(r);
    for (int pass = 0; pass < 4; ++pass) {
      auto pending = page.requests;
      page.requests.clear();
      for (auto &pending_request : pending) {
        auto it = resources.find(pending_request.url);
        if (it != resources.end())
          page.resource(it->second);
        else {
          Resource r;
          r.url = pending_request.url;
          r.ok = false;
          r.error = "resource not supplied to headless renderer";
          page.resource(r);
        }
      }
    }
    // Interactions operate on an already-laid-out page, like the shell. A
    // render action explicitly snapshots intermediate state for damage tests.
    if (!actions.empty()) page.render(width, height, scroll);
    for (const std::string &action : actions) {
      const size_t colon = action.find(':');
      const std::string type = action.substr(0, colon);
      const std::string value = colon == std::string::npos ? "" : action.substr(colon + 1);
      if (type == "render") page.render(width, height, scroll);
      else if (type == "key") page.key(value, "");
      else if (type == "text") page.key("", value);
      else if (type == "hover" || type == "click" || type == "wheel") {
        const auto values = split(value, ',');
        if (values.size() != (type == "wheel" ? 3u : 2u))
          throw std::runtime_error("invalid interaction coordinates");
        const double x = std::stod(values[0]), y = std::stod(values[1]);
        if (type == "hover") page.pointer_move(x, y);
        else if (type == "click") page.click(x, y);
        else page.wheel(x, y, std::stod(values[2]));
      } else throw std::runtime_error("unknown action: " + action);
    }
    auto frame = page.render(width, height, scroll);
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
      c.font_family = j.str("fontFamily", "Segoe UI");
      c.font_weight = int(j.num("fontWeight"));
      c.node = int(j.num("node"));
      c.url = j.str("url");
      c.gradient = j.str("gradient");
      c.fixed = j.flag("fixed");
      c.radius = j.num("radius");
      c.opacity = j.num("opacity", 1);
      c.blur = j.num("blur");
      c.spread = j.num("spread");
      c.offset_x = j.num("offsetX");
      c.offset_y = j.num("offsetY");
      c.rotation = j.num("rotation");
      c.filter = j.str("filter");
      commands.push_back(c);
    }
    // A canvas paints its own bitmap, so I fold those into the same lookup the
    // paint pass uses for images.
    std::map<std::string, Image> pictures = page.images();
    for (const auto &[node, image] : page.canvas_images())
      pictures["canvas:" + std::to_string(node)] = image;
    paint_commands(canvas, commands, scroll, pictures);
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
