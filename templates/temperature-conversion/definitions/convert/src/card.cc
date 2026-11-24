#include "card.hh"

auto run(const Card::Inputs&) -> Card::Output {
  const double fahrenheit = Card::params.value * 1.8 + 32.0;
  card::display::send(card::display::Scalar{fahrenheit, "°F"});
  Card::Output output;
  output.fahrenheit.emplace().value = fahrenheit;
  return output;
}
