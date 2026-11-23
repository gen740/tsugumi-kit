#include "card.hh"

auto run(const Card::Inputs& input) -> Card::Output {
  using Fallback = decltype(Card::Output::fahrenheit)::value_type;
  using Celsius = Card::InputType<"celsius", Fallback>;
  const auto celsius = input.get<Celsius>("celsius");
  const double fahrenheit = celsius.value * 1.8 + 32.0;
  card::display::send(card::display::Scalar{fahrenheit, "°F"});
  Card::Output output;
  output.fahrenheit.emplace().value = fahrenheit;
  return output;
}
