#include "card.hh"

auto run(const Card::Inputs& input) -> Card::Output {
  const double fahrenheit = input.celsius.value * 1.8 + 32.0;
  card::display(card::Scalar{fahrenheit, "°F"});
  Card::Output output;
  output.fahrenheit.emplace().value = fahrenheit;
  return output;
}
